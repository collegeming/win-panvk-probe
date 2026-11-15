# Third-party source notice

This repository vendors a small number of files from other projects so the
probe can be built as a standalone Windows ARM64 executable.

## MIT-licensed sources

- `src/third_party/vdrm/vdrm.h`
- `src/third_party/vdrm/vdrm.c`
- `src/compat/virtio/virtio-gpu/drm_hw.h`

  Mesa vdrm / virtio-gpu DRM headers.
  Copyright © 2022-2023 Google, Inc.
  SPDX-License-Identifier: MIT

- `src/third_party/vdrm/vdrm_wddm.c`

  DroidVM / Mesa "wingpu" WDDM transport backend for Windows on ARM64.
  Copyright © 2026 DroidVM contributors.
  SPDX-License-Identifier: MIT

  Local modifications for this probe:
  * context-type gate relaxed from `context_type == MSM` to "requested type
    must equal the host-reported type" so the Mali `KBAS` (0x4b424153) backend
    can be selected;
  * unconditional raw-stage diagnostics on stderr (adapter scan, capset words,
    CreateDevice/CreateContext/ContextInit/CreateAllocation/BlobMap statuses,
    ring handles and rsp_mem_offset);
  * `vdrm_wddm_probe_state()` records those values for the structured report;
  * the `align(x, a)` call in `vdrm.c` was renamed to `vdrm_align()` because the
    Windows SDK's `DECLSPEC_ALIGN(x)` expands to `__declspec(align(x))` and
    would otherwise rescan Mesa's two-argument `align` macro with one argument.

The probe itself (`src/probe_main.c`, `src/compat/**`, workflow, docs) is
released under the MIT license.

## AGPL-3.0 source

- `src/third_party/vdrm/virtio_wddm_uapi.h`

  VirtIO-GPU WDDM KMD user-mode UAPI header from virtio-d3d11
  (https://github.com/anonymix007/virtio-d3d11), used by its AGPL-3.0 KMD.
  The upstream project is licensed AGPL-3.0 (see its README).

  This file is included only for the private, internal M1 experiment.  It is
  not intended for redistribution.  If this repository or the probe binary is
  ever distributed, the AGPL-3.0 obligations of that header apply to the
  combined work; relicense or replace the header before doing so.
