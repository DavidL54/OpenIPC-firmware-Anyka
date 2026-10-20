# Proposed small contributions

No PR has been created. Do not submit the full source snapshot as one firmware PR.

| Order | Target | Proposed scope | Preconditions |
| --- | --- | --- | --- |
| 1 | wiki / builder | Verified model, CPU, partitions, sensor, GPIO/motor ABI, unresolved pinout | Redacted facts reviewed, exact board identity |
| 2 | ipctool | Identification/probe improvements if missing | Read-only behavior and source attribution |
| 3 | owning upstream packages | Small independently useful ABI/time/SDK fixes | Maintainer agreement, license/source available |
| 4 | builder | Board-specific GPIO/mux/sensor configuration | Agreed runtime and standard provisioning |
| 5 | firmware | Additive Anyka package/target wiring and CI registration | No preload/blob hooks, reproducible dependencies, build/boot reports |
| 6 | linux if necessary | Exact kernel changes/config support | Corresponding sources and physical boot validation |

For each PR, name the baseline commit and changed boards; describe why the shared blast radius is limited; attach exact build sizes/hashes and hardware observations; state unverified behavior. Do not move this board's defaults into a shared load script. Do not add a package that nothing selects. Keep kernel source changes in the kernel repository.

A minimal runtime package should have Config.in, a matching .mk, source/version/hash/license metadata, install rules, selected defconfig and CI registration. A fabricated recipe pointing to unresolved proprietary input is not a working integration, so no pretend drop-in recipe is shipped here.
