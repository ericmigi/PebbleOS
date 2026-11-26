PebbleOS builds Samsa from the upstream submodule at `80f246c`. The build
applies `sdk-dispatch.patch` in its own source directory to support the SDK
dispatch breakpoint from `83ae653`. The default interpreter has an empty
decode cache on X4 Pro.

Configure with `pbl configure --board xteink_x4_pro -DCONFIG_SAMSA_FAST64=y`
to add the optimized interpreter derived from `88ef2b7` and use a 64-entry
decode cache. Its direct fetch window stops at the SDK instruction breakpoint.
Both configurations use the same submodule checkout.
