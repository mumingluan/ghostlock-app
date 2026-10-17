/*
 * Host vectors for ResolvedAddresses: the DRAM-base (phys_offset) override and
 * its effect on the image->direct-map translation. No Android headers are
 * needed; init_for_soc takes the SoC family explicitly.
 */

#include "kernel/target.h"
#include "memory/address_space.h"
#include "profile/model.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {
    using ghostlock::memory::ResolvedAddresses;
    using ghostlock::memory::SocFamily;

    ghostlock::profile::kernel_offsets make_transport(uint64_t init_cred,
                                                      std::optional<uint64_t> phys_load,
                                                      std::optional<uint64_t> phys_offset) {
        ghostlock::profile::kernel_offsets values{};
        values.uname_r = "test";
        values.offsets.init_cred = init_cred;
        values.misc.kernel_phys_load = phys_load;
        values.misc.kernel_phys_offset = phys_offset;
        return values;
    }
}

int main() {
    using ghostlock::kernel::KIMAGE_TEXT_BASE;
    using ghostlock::kernel::P0_PAGE_OFFSET;
    using ghostlock::kernel::P0_PHYS_OFFSET;

    const uint64_t off = 0x237a0e8; /* e.g. selinux_enforcing */
    const uint64_t image = KIMAGE_TEXT_BASE + off;

    /* MTK6893 (moto): image phys 0x40080000, DRAM base 0x40000000. With the
     * profile override the translation resolves; without it (default
     * P0_PHYS_OFFSET = 0x80000000) the physical sits below the base and the
     * translation is rejected. */
    {
        const auto transport = make_transport(0x1000, 0x40080000ULL, 0x40000000ULL);
        ghostlock::profile::TargetProfile profile(transport);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(&profile, SocFamily::Mtk) == 0);
        assert(addresses.phys_offset == 0x40000000ULL);
        const uintptr_t alias = addresses.data_alias(image);
        assert(alias == (static_cast<uintptr_t>(0x80000ULL + off) | P0_PAGE_OFFSET));
        assert(alias != 0);
    }

    {
        const auto transport = make_transport(0x1000, 0x40080000ULL, std::nullopt);
        ghostlock::profile::TargetProfile profile(transport);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(&profile, SocFamily::Mtk) == 0);
        assert(addresses.phys_offset == P0_PHYS_OFFSET);
        assert(addresses.data_alias(image) == 0);
    }

    /* Absent override keeps the compiled default for every existing device. */
    {
        const auto transport = make_transport(0x1000, 0xa8000000ULL, std::nullopt);
        ghostlock::profile::TargetProfile profile(transport);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(&profile, SocFamily::Qcom) == 0);
        assert(addresses.phys_offset == P0_PHYS_OFFSET);
    }

    /* A DRAM base must be GiB-aligned (memstart_addr semantics): the iQOO 15
     * profile shipped 0x81960000 — the first System RAM bank from
     * /proc/iomem, not the DRAM base — which shifted every kernel write and
     * crashed the device in three consecutive runs. Unaligned and zero
     * overrides are rejected instead of reaching the write path. */
    {
        const auto transport = make_transport(0x1000, 0xc7800000ULL, 0x81960000ULL);
        ghostlock::profile::TargetProfile profile(transport);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(&profile, SocFamily::Qcom) == -1);
    }

    {
        const auto transport = make_transport(0x1000, 0xc7800000ULL, 0ULL);
        ghostlock::profile::TargetProfile profile(transport);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(&profile, SocFamily::Qcom) == -1);
    }

    /* The corrected value (0x80000000) passes and matches the compiled
     * default's translation. */
    {
        const auto transport = make_transport(0x1000, 0xc7800000ULL, 0x80000000ULL);
        ghostlock::profile::TargetProfile profile(transport);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(&profile, SocFamily::Qcom) == 0);
        assert(addresses.phys_offset == P0_PHYS_OFFSET);
    }

    std::puts("address_space_test: ok");
    return 0;
}
