/*
 * vdrm-probe: Windows ARM64 user-mode probe for the DroidVM virtio WDDM KMD
 * (virtio_wddm_rs.sys) carrying a Mali kbase native-context (KBAS) kbv RPC.
 *
 * It performs the full handshake described in the M1 milestone:
 *   1. D3DKMT adapter scan / identification (raw values logged by the
 *      transport copy in src/third_party/vdrm/vdrm_wddm.c);
 *   2. OpenAdapterFromLuid -> CreateDevice -> CreateContext;
 *   3. DRM capset (id 6): wire_format_version, context_type, version;
 *   4. ContextInit(capset_id=DRM(6), num_rings=1);
 *   5. HOST3D|MAPPABLE 0x4000 blob (blob_id=0) + BlobMap; read the host-written
 *      rsp_mem_offset (expected 8);
 *   6. kbv RPC cmd=9 (GPU props) and cmd=10 (CSIF) through an ExecBuffer
 *      escape, waiting on shmem->seqno and reading the response from rsp_mem;
 *   7. two connect/teardown rounds in one process, to catch stale pages after
 *      the KMD/host reuses a BAR/shmem offset.
 *
 * The transport sources are copies of Mesa vdrm (MIT) and DroidVM's
 * vdrm_wddm.c (MIT); see README.md and NOTICE.md.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdrm.h"
#include "vdrm_probe.h"

#include "util/os_time.h"
#include "util/u_math.h"

#define KBAS_CONTEXT_TYPE 0x4b424153u  /* "KBAS", host Mali kbase backend */
#define PROBE_SHMEM_SIZE 0x4000u
#define DEFAULT_TIMEOUT_MS 10000u
#define PROPS_EXPECTED_SIZE 781u
#define CSIF_EXPECTED_SIZE 24u

/* Wire layout shared by guest PanVK (mali_kbase_virtio.c) and the host
 * mali_kbase_renderer.c: a vdrm_ccmd_req header followed by the kbv fields.
 * Both sides assert the same sizes. */
struct kbv_req {
   uint32_t cmd, len, seqno, rsp_off;
   uint32_t handle, offset, size, reserved;
   uint8_t data[1024];
};

struct kbv_rsp {
   uint32_t len;
   int32_t status;
   uint32_t handle, size;
   uint64_t value;
   uint8_t data[1024];
};

_Static_assert(sizeof(struct kbv_req) == 1056, "kbv_req wire size");
_Static_assert(sizeof(struct kbv_rsp) == 1048, "kbv_rsp wire size");
_Static_assert(offsetof(struct kbv_req, data) == 32, "kbv_req.data offset");
_Static_assert(offsetof(struct kbv_rsp, value) == 16, "kbv_rsp.value offset");
_Static_assert(offsetof(struct kbv_rsp, data) == 24, "kbv_rsp.data offset");

static const char *
arch_name(void)
{
#if defined(_M_ARM64) || defined(__aarch64__)
   return "ARM64";
#elif defined(_M_X64) || defined(__x86_64__)
   return "x64";
#else
   return "unknown";
#endif
}

static void
hexdump(const char *label, const void *data, size_t len)
{
   const uint8_t *p = data;
   size_t i, j;

   printf("  %s (%zu bytes):\n", label, len);
   for (i = 0; i < len; i += 16) {
      printf("    %04zx:", i);
      for (j = 0; j < 16; j++) {
         if (i + j < len)
            printf(" %02x", p[i + j]);
         else
            printf("   ");
      }
      printf("  ");
      for (j = 0; j < 16 && i + j < len; j++) {
         uint8_t c = p[i + j];
         putchar(c >= 32 && c < 127 ? c : '.');
      }
      putchar('\n');
   }
}

static bool
seqno_before(uint32_t a, uint32_t b)
{
   return (int32_t)(a - b) < 0;
}

static uint32_t
timeout_ms_from_env(void)
{
   const char *s = getenv("VDRM_PROBE_TIMEOUT_MS");
   char *end = NULL;
   unsigned long v;

   if (!s || !*s)
      return DEFAULT_TIMEOUT_MS;
   v = strtoul(s, &end, 0);
   if (end == s || v == 0 || v > 600000)
      return DEFAULT_TIMEOUT_MS;
   return (uint32_t)v;
}

static void
print_adapter_state(const struct vdrm_wddm_probe_state *st)
{
   printf("[stage 1] D3DKMT adapter scan\n");
   printf("  EnumAdapters2: status=0x%08lx num_adapters=%lu\n",
          (ULONG)st->enum_status, (unsigned long)st->num_adapters);
   if (!st->adapter_found) {
      printf("  no virtio adapter matched tag=0x%016llx\n",
             (unsigned long long)VIRTIO_WDDM_ADAPTER_INFO_TAG);
      return;
   }
   printf("  selected LUID = %08lx:%08lx\n",
          st->luid.HighPart, st->luid.LowPart);
   printf("  QueryAdapterInfo(UMDRIVERPRIVATE): status=0x%08lx\n",
          (ULONG)st->adapter_query_status);
   printf("  adapter_info.tag          = 0x%016llx (\"%.8s\")\n",
          (unsigned long long)st->adapter_info.tag,
          (const char *)&st->adapter_info.tag);
   printf("  adapter_info.capset_mask  = 0x%016llx (DRM bit %s)\n",
          (unsigned long long)st->adapter_info.capset_mask,
          (st->adapter_info.capset_mask & VIRTIO_WDDM_CAPSET_MASK_DRM) ? "set" : "MISSING");
   printf("  adapter_info.supports_3d  = %u\n", st->adapter_info.supports_3d ? 1u : 0u);
   printf("  adapter_info.has_shmem    = %u\n", st->adapter_info.has_shmem ? 1u : 0u);
}

static bool
print_caps(const struct vdrm_wddm_probe_state *st)
{
   const struct virgl_renderer_capset_drm *c = &st->caps;

   printf("[stage 2] DRM capset id=6\n");
   printf("  capset escape status   = 0x%08lx\n", (ULONG)st->capset_status);
   printf("  wire_format_version    = %u (expected 2)\n", c->wire_format_version);
   printf("  version                = %u.%u.%u\n",
          c->version_major, c->version_minor, c->version_patchlevel);
   printf("  context_type           = 0x%08x (\"%.4s\") (expected 0x%08x \"KBAS\")\n",
          c->context_type, (const char *)&c->context_type, KBAS_CONTEXT_TYPE);
   printf("  raw u32[0..5]          = 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
          ((const uint32_t *)c)[0], ((const uint32_t *)c)[1],
          ((const uint32_t *)c)[2], ((const uint32_t *)c)[3],
          ((const uint32_t *)c)[4], ((const uint32_t *)c)[5]);
   printf("  ContextInit status     = 0x%08lx\n", (ULONG)st->ctx_init_status);

   if (c->wire_format_version != 2 || c->context_type != KBAS_CONTEXT_TYPE) {
      printf("  CAPSET CRITERION FAILED\n");
      return false;
   }
   printf("  CAPSET CRITERION OK\n");
   return true;
}

static void
print_shmem(const struct vdrm_wddm_probe_state *st, struct vdrm_device *vdev)
{
   uint32_t offset = (uint32_t)((const uint8_t *)vdev->rsp_mem -
                                (const uint8_t *)vdev->shmem);

   printf("[stage 3] shared ring (HOST3D|MAPPABLE blob_id=0 size=0x%x)\n",
          PROBE_SHMEM_SIZE);
   printf("  CreateAllocation status = 0x%08lx\n", (ULONG)st->shmem_alloc_status);
   printf("  KMD allocation handle   = 0x%x\n", st->shmem_kmt);
   printf("  virtio resource id      = %u\n", st->shmem_res_id);
   printf("  BlobMap status          = 0x%08lx\n", (ULONG)st->shmem_map_status);
   printf("  CPU mapping             = %p\n", (void *)vdev->shmem);
   printf("  shmem->seqno            = %u\n", vdev->shmem->seqno);
   printf("  shmem->rsp_mem_offset   = %u (expected 8)\n",
          vdev->shmem->rsp_mem_offset);
   printf("  rsp_mem                 = %p\n", (void *)vdev->rsp_mem);
   printf("  rsp_mem_len             = %u\n", vdev->rsp_mem_len);
   printf("  offset recomputed from pointers = %u\n", offset);
}

/*
 * Submit one kbv_req as a single ExecBuffer escape (ring 0), then poll the
 * host-written shmem seqno with a bounded wait and copy the response back.
 */
static int
kbv_rpc(struct vdrm_device *vdev, struct kbv_req *req, struct kbv_rsp *rsp,
        uint32_t timeout_ms)
{
   struct vdrm_ccmd_req *hdr = (struct vdrm_ccmd_req *)req;
   struct vdrm_execbuf_params params = {0};
   uint32_t rsp_size = (uint32_t)align(sizeof(*rsp), 8);
   uint64_t start, elapsed_ms;
   int ret;

   memset(rsp, 0, sizeof(*rsp));
   if (!vdrm_alloc_rsp(vdev, hdr, rsp_size)) {
      printf("  kbv_rpc: vdrm_alloc_rsp(%u) failed\n", rsp_size);
      return -ENOMEM;
   }

   req->len = sizeof(*req);
   params.ring_idx = 0;
   params.req = hdr;

   ret = vdrm_execbuf(vdev, &params);
   printf("  tx: cmd=%u len=%u seqno=%u rsp_off=%u handle=%u offset=%u size=%u reserved=%u\n",
          req->cmd, req->len, req->seqno, req->rsp_off, req->handle,
          req->offset, req->size, req->reserved);
   if (ret) {
      printf("  vdrm_execbuf failed: ret=%d (errno=%d)\n", ret, errno);
      return ret;
   }

   start = os_time_get_nano();
   for (;;) {
      uint32_t host_seqno = vdev->shmem->seqno;

      if (!seqno_before(host_seqno, hdr->seqno)) {
         printf("  host seqno=%u reached target=%u\n", host_seqno, hdr->seqno);
         break;
      }
      elapsed_ms = (os_time_get_nano() - start) / 1000000ull;
      if (elapsed_ms >= timeout_ms) {
         printf("  TIMEOUT waiting for shmem seqno: host=%u target=%u elapsed=%" PRIu64 " ms\n",
                vdev->shmem->seqno, hdr->seqno, elapsed_ms);
         return -ETIMEDOUT;
      }
      Sleep(1);
   }

   MemoryBarrier();
   memcpy(rsp, (uint8_t *)vdev->rsp_mem + hdr->rsp_off, sizeof(*rsp));

   printf("  rx: len=%u status=%d handle=%u size=%u value=%" PRIu64 "\n",
          rsp->len, rsp->status, rsp->handle, rsp->size, rsp->value);
   if (rsp->len != sizeof(*rsp)) {
      printf("  response length mismatch: got %u expected %zu\n",
             rsp->len, sizeof(*rsp));
      return -EPROTO;
   }
   return 0;
}

/* Best-effort decode of the KBASE_GPUPROP_* stream: u32 key, then 1<<(key&3)
 * value bytes.  Bounded so truncated/garbage host data cannot run away. */
static void
decode_gpuprops(const uint8_t *data, size_t len)
{
   size_t off = 0;
   unsigned shown = 0;

   printf("  gpuprops entries (key/id/size/value), first 8:\n");
   while (off + 4 <= len && shown < 8) {
      uint32_t key;
      uint32_t bytes, id;
      uint64_t value = 0;

      memcpy(&key, data + off, 4);
      off += 4;
      bytes = 1u << (key & 3u);
      id = key >> 2;
      if (off + bytes > len) {
         printf("    [%u] truncated: key=0x%08x needs %u bytes, %zu left\n",
                shown, key, bytes, len - off);
         break;
      }
      memcpy(&value, data + off, bytes);
      printf("    [%u] key=0x%08x id=%u size=%u value=%" PRIu64 " (0x%0*" PRIx64 ")\n",
             shown, key, id, bytes, value, (int)(bytes * 2), value);
      off += bytes;
      shown++;
   }
   if (off < len)
      printf("    ... %zu more bytes not decoded\n", len - off);
}

static int
probe_props(struct vdrm_device *vdev, uint32_t timeout_ms, bool *criterion_ok)
{
   struct kbv_req req;
   struct kbv_rsp rsp;
   uint8_t *props = NULL;
   uint32_t total, off;
   int ret;

   *criterion_ok = false;
   printf("[stage 4] kbv RPC cmd=9 (GPU props: size query)\n");
   memset(&req, 0, sizeof(req));
   req.cmd = 9;
   ret = kbv_rpc(vdev, &req, &rsp, timeout_ms);
   if (ret)
      return ret;
   if (rsp.status != 0) {
      printf("  props size query failed: status=%d\n", rsp.status);
      return -EIO;
   }
   total = (uint32_t)rsp.value;
   if (!total || total > 65536) {
      printf("  props size invalid: value=%u (expected >0, <=65536)\n", total);
      return -EPROTO;
   }
   printf("  props total size = %u (expected %u)\n", total, PROPS_EXPECTED_SIZE);

   props = (uint8_t *)malloc(total);
   if (!props)
      return -ENOMEM;

   for (off = 0; off < total; ) {
      uint32_t chunk = total - off > 1024 ? 1024 : total - off;

      memset(&req, 0, sizeof(req));
      req.cmd = 9;
      req.offset = off;
      req.size = chunk;
      ret = kbv_rpc(vdev, &req, &rsp, timeout_ms);
      if (ret) {
         free(props);
         return ret;
      }
      if (rsp.status != 0 || rsp.size == 0 || rsp.size > chunk ||
          rsp.size > total - off) {
         printf("  props chunk failed: status=%d size=%u chunk=%u off=%u\n",
                rsp.status, rsp.size, chunk, off);
         free(props);
         return -EPROTO;
      }
      memcpy(props + off, rsp.data, rsp.size);
      off += rsp.size;
   }

   hexdump("props", props, total < 64 ? total : 64);
   decode_gpuprops(props, total);
   free(props);

   if (total == PROPS_EXPECTED_SIZE)
      printf("  PROPS CRITERION OK (returned %u bytes)\n", total);
   else
      printf("  PROPS CRITERION OK (non-zero status=0 size=%u; canonical value is %u)\n",
             total, PROPS_EXPECTED_SIZE);
   *criterion_ok = true;
   return 0;
}

static int
probe_csif(struct vdrm_device *vdev, uint32_t timeout_ms, bool *criterion_ok)
{
   struct kbv_req req;
   struct kbv_rsp rsp;
   int ret;

   *criterion_ok = false;
   printf("[stage 5] kbv RPC cmd=10 (CSIF info)\n");
   memset(&req, 0, sizeof(req));
   req.cmd = 10;
   ret = kbv_rpc(vdev, &req, &rsp, timeout_ms);
   if (ret)
      return ret;
   if (rsp.status != 0) {
      printf("  CSIF failed: status=%d\n", rsp.status);
      return -EIO;
   }
   if (rsp.size != CSIF_EXPECTED_SIZE) {
      printf("  CSIF size mismatch: got %u expected %u\n",
             rsp.size, CSIF_EXPECTED_SIZE);
      return -EPROTO;
   }

   hexdump("csif", rsp.data, CSIF_EXPECTED_SIZE);
   {
      uint32_t words[6];
      memcpy(words, rsp.data, sizeof(words));
      printf("  csif u32[0..5] = %u %u %u %u %u %u\n",
             words[0], words[1], words[2], words[3], words[4], words[5]);
      printf("  decoded: group_num=%u stream_num=%u doorbell_size=%u "
             "pad=%u doorbell_stride=%u extract_stride=%u\n",
             words[0], words[1], words[2], words[3], words[4], words[5]);
   }
   printf("  CSIF CRITERION OK (returned %u bytes)\n", rsp.size);
   *criterion_ok = true;
   return 0;
}

static int
run_round(int round, uint32_t timeout_ms, bool *caps_ok, bool *props_ok,
          bool *csif_ok)
{
   const struct vdrm_wddm_probe_state *st;
   struct vdrm_device *vdev;
   int ret = 0;

   printf("\n=== round %d/%s ===\n", round, "connect");
   vdev = vdrm_device_connect(-1, KBAS_CONTEXT_TYPE);
   st = vdrm_wddm_probe_state();

   print_adapter_state(st);
   if (!vdev) {
      printf("  CONNECT FAILED at stage '%s'\n",
             st->failed_stage ? st->failed_stage : "(unknown)");
      return -1;
   }
   printf("  vdrm_device_connect OK: adapter=0x%x device=0x%x context=0x%x\n",
          st->h_adapter, st->h_device, st->h_context);

   *caps_ok = print_caps(st);
   print_shmem(st, vdev);

   if (probe_props(vdev, timeout_ms, props_ok))
      ret = -1;
   if (probe_csif(vdev, timeout_ms, csif_ok))
      ret = -1;

   vdrm_device_close(vdev);
   printf("  teardown complete\n");
   return ret;
}

static void
usage(const char *argv0)
{
   printf("usage: %s [--rounds N] [--timeout-ms N]\n", argv0);
   printf("  --rounds N      connect/teardown rounds in one process (default 2)\n");
   printf("  --timeout-ms N  per-RPC shmem seqno timeout (default %u)\n",
          DEFAULT_TIMEOUT_MS);
}

int
main(int argc, char **argv)
{
   int rounds = 2;
   uint32_t timeout_ms = timeout_ms_from_env();
   bool all_caps = true, all_props = true, all_csif = true, all_rounds = true;
   int i, round;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--rounds") && i + 1 < argc) {
         rounds = atoi(argv[++i]);
         if (rounds < 1 || rounds > 16) {
            printf("invalid --rounds %d (1..16)\n", rounds);
            return 2;
         }
      } else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) {
         timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 0);
         if (!timeout_ms)
            timeout_ms = DEFAULT_TIMEOUT_MS;
      } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
         usage(argv[0]);
         return 0;
      } else {
         usage(argv[0]);
         return 2;
      }
   }

   /* Keep the transport's own escape-level diagnostics visible unless the
    * caller explicitly disables them. */
   if (!getenv("VDRM_WDDM_DEBUG"))
      _putenv_s("VDRM_WDDM_DEBUG", "1");

   printf("vdrm-probe: DroidVM virtio WDDM KMD + Mali kbase kbv RPC probe\n");
   printf("  build arch       = %s\n", arch_name());
   printf("  context_type     = 0x%08x (\"KBAS\")\n", KBAS_CONTEXT_TYPE);
   printf("  rounds           = %d\n", rounds);
   printf("  rpc timeout      = %u ms\n", timeout_ms);
   printf("  shmem blob       = 0x%x bytes, blob_id=0, HOST3D|MAPPABLE\n",
          PROBE_SHMEM_SIZE);

   for (round = 1; round <= rounds; round++) {
      bool caps = false, props = false, csif = false;

      if (run_round(round, timeout_ms, &caps, &props, &csif))
         all_rounds = false;
      all_caps &= caps;
      all_props &= props;
      all_csif &= csif;
   }

   printf("\n=== summary ===\n");
   printf("  capset fields (wire_format_version=2, context_type=0x4b424153): %s\n",
          all_caps ? "PASS" : "FAIL");
   printf("  kbv props (cmd=9, status=0, size > 0, expected %u): %s\n",
          PROPS_EXPECTED_SIZE, all_props ? "PASS" : "FAIL");
   printf("  kbv CSIF  (cmd=10, status=0, %u bytes): %s\n",
          CSIF_EXPECTED_SIZE, all_csif ? "PASS" : "FAIL");
   printf("  all %d connect/teardown rounds: %s\n", rounds,
          all_rounds ? "PASS" : "FAIL");
   printf("RESULT: %s\n",
          (all_caps && all_props && all_csif && all_rounds) ? "PASS" : "FAIL");

   return (all_caps && all_props && all_csif && all_rounds) ? 0 : 1;
}
