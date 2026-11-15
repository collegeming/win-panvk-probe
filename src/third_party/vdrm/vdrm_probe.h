/*
 * Probe-only inspection API for the vendored vdrm WDDM backend.
 *
 * vdrm_wddm.c is a private, minimally modified copy of DroidVM/Mesa's MIT
 * transport.  It records the raw NTSTATUS and KMD-reported values of each
 * connection stage here so the probe can print them even when connect fails
 * before returning a vdrm_device.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef VDRM_WDDM_PROBE_H
#define VDRM_WDDM_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UMDF_USING_NTSTATUS
#define UMDF_USING_NTSTATUS
#endif
#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <d3dkmthk.h>

#include "vdrm.h"
#include "virtio_wddm_uapi.h"

struct vdrm_wddm_probe_state {
   /* Stage 1: adapter enumeration / identification. */
   bool enum_called;
   NTSTATUS enum_status;
   ULONG num_adapters;
   bool adapter_found;
   LUID luid;
   bool adapter_info_valid;
   NTSTATUS adapter_query_status;
   VIRTIO_WDDM_AdapterInfo adapter_info;

   /* Stage 2: open / device / context / capset / ContextInit. */
   NTSTATUS open_status;
   NTSTATUS device_status;
   NTSTATUS context_status;
   uint32_t h_adapter;
   uint32_t h_device;
   uint32_t h_context;
   NTSTATUS capset_status;
   bool caps_valid;
   struct virgl_renderer_capset_drm caps;
   NTSTATUS ctx_init_status;

   /* Stage 3: shmem ring blob. */
   NTSTATUS shmem_alloc_status;
   NTSTATUS shmem_map_status;
   uint32_t shmem_kmt;
   uint32_t shmem_res_id;
   uint32_t shmem_size;
   uint32_t rsp_mem_offset;
   void *shmem_cpu;

   const char *failed_stage;
};

const struct vdrm_wddm_probe_state *vdrm_wddm_probe_state(void);

#endif /* VDRM_WDDM_PROBE_H */
