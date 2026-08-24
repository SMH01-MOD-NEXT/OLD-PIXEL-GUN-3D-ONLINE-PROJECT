#pragma once

// 23.1.3 experiment: point the shipped asset-bundle Downloader at the 24.0.1
// resource set and log everything it asks the CDN for.
//
// The module installs itself from its own translation unit, so it does not have
// to be wired into main.cpp (the CMake target globs *.cpp). The entry point is
// exported anyway so it can be called from the regular bootstrap if the
// experiment is ever promoted out of this branch.
//
// Scope: every hook lives inside PGCompany.AssetBundles_v3. UpdatesChecker
// (TypeDefIndex 6204) and ClientUpdateBannerWindow (TypeDefIndex 2099) are
// never referenced, resolved or patched here - hooking those prevents 23.1.3
// from starting at all.

namespace res_2401_experiment_2313 {

// Installs the asset-bundle hooks. Idempotent: repeated calls only retry the
// targets that are not hooked yet, so the self-bootstrap can poll while IL2CPP
// metadata is still settling.
bool install_hooks();

} // namespace res_2401_experiment_2313
