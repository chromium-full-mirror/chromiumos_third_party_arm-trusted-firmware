FIRME
=====

This page summarizes the current status of the FIRME implementation in TF-A.

Background
----------

FIRME defines standard EL3 firmware interfaces used by software above EL3 to
manage capabilities associated with Arm Confidential Compute Architecture
(Arm CCA) and, in particular, the Realm Management Extension (FEAT_RME).
These interfaces cover the EL3 dependencies needed for Realm-related flows such
as resource assignment and isolation, Realm construction, and attestation.

The specification groups these interfaces into FIRME services. The service
areas defined by the specification are:

- granule protection table management
- IDE key management for PCIe and CXL root ports
- memory encryption context management
- attestation token management
- integrated device management

In addition to those service-specific interfaces, FIRME defines a base service
that provides common concepts plus version and feature discovery for the other
services.

FIRME sits on the EL3 boundary and is intended to be callable from the
Non-secure, Realm, and Secure security states. A FIRME implementation is also
permitted on systems that do not implement FEAT_RME, for example when parts of
the interface are still useful independently of Realm support.

TF-A currently implements the Arm FIRME specification version ``1.0 alp2`` as
defined by `DEN0149`_.

FIRME is exposed as a 64-bit fast SMC standard service. The currently allocated
TF-A function ID range is ``0xC4000400`` to ``0xC4000412``.

Build and integration
---------------------

FIRME support in TF-A is experimental:

- ``FIRME_SUPPORT`` enables support in builds.
- When build with ``FIRME_SUPPORT``, RME supported system will make use of the
  FIRME granule management services.
- When FIRME support is enabled in a RMM enabled build, TRP uses
  ``FIRME_GM_GPI_SET`` for granule transitions instead of the legacy
  delegate/undelegate interface.

Current status
--------------

- Only a single FIRME version is supported per service.
- The FIRME base service implements version and feature discovery through
  ``FIRME_SERVICE_VERSION`` and ``FIRME_SERVICE_FEATURES``.
- Granule management implements ``FIRME_GM_GPI_SET`` and Realm-only
  ``FIRME_GM_L1_GPT_CREATE`` / ``FIRME_GM_L1_GPT_DESTROY`` on platforms
  with lifecycle support.
- The IDE key management, MECID management, attestation, and integrated device
  management services are not implemented yet.
- ``FIRME_GM_GPI_OP_CONTINUE_FID`` is mandatory in the ``1.0 alp2`` specification,
  but it is not implemented yet.

As a result, the current implementation tracks FIRME ``1.0 alp2`` for
versioning purposes, but it does not yet provide all mandatory functionality
defined by that specification revision.

Implementation details
----------------------

- TF-A currently advertises FIRME support for Secure, Non-secure, and Realm
  callers.
- The base service is the entry point used to discover per-service versions and
  feature registers.
- The granule management service publishes two feature registers:

  - register 0 advertises ``FIRME_GM_GPI_SET`` and, for Realm callers on
    supported platforms, ``FIRME_GM_L1_GPT_CREATE`` and
    ``FIRME_GM_L1_GPT_DESTROY``
  - register 1 is populated at runtime from ``GPCCR_EL3`` and reports platform
    granule protection geometry such as page size, ``L0GPTSZ``, and ``PPS``

- ``FIRME_GM_GPI_SET`` is implemented on top of TF-A's generic GPT transition
  helper and returns the number of successfully transitioned granules.

Granule management geometry
~~~~~~~~~~~~~~~~~~~~~~~~~~

The internal ``firme_gm_geometry.h`` interface provides immutable GPT geometry.
Initialization validates and caches ``GPCCR_EL3`` and ``GPTBR_EL3`` before
granule management requests are accepted. Consumers query sizes, counts and
address calculations without accessing the backend's representation.

PAR and L1-object validators check alignment, extent overflow and PPS bounds.
L1 storage must be nonzero, table-aligned and occupy a whole number of granules.
An extent ending exactly at PPS is geometrically valid. These checks do not
establish ownership, live descriptor state or mapping availability.

The L0 address query validates a PAR base and returns the descriptor address;
the caller performs any descriptor access. L1 index and GPI-shift queries take
an already validated granule address. Geometry performs no GPT mutations,
reservation operations or cache maintenance.

L1 GPT lifecycle
~~~~~~~~~~~~~~~~

CREATE reserves the target PAR address, transitions the complete
Non-secure donor to Root, initializes every L1 entry from the original block
GPI, then publishes the table descriptor. A platform policy distinguishes
Host-managed PARs from EL3-managed ranges. FVP keeps the first 4 GiB and its
PCIe aperture under EL3 management. A donor cannot describe its own PAR.
Dynamic Root mappings and valid platform policy are required for these features.

DESTROY reserves the target PAR address, discovers the donor, verifies Root
ownership and uniform valid table contents, then replaces the table descriptor
with a block. Full-PAR invalidation completes before the detached donor is
scrubbed. It then removes the Root mapping and returns every donor granule to
Non-secure. X2 returns the exact donor address on success and zero on an
ordinary error; X1 is reserved and returned as zero. Once detached, a terminal
release error is fatal.

CREATE and DESTROY share a target-address reservation registry with opaque,
registry-owned handles. Release clears the caller's handle. Reservations
remain held through cleanup and rollback. Donor granules are not reserved;
GPI_SET and legacy delegation calls do not use the registry.

Table operations and granule transitions share ``firme_gm_gpi.c`` and its
descriptor helpers. The table interface retains caller-held target admission
and its own initialization, validation, publication and scrubbing contracts.

RMM lifecycle discovery requires ``GPT_INFO``, which is not implemented on this
branch.

--------------

*Copyright (c) 2026, Arm Limited and Contributors. All rights reserved.*

.. _DEN0149: https://developer.arm.com/documentation/den0149/1-0alp2/?lang=en
