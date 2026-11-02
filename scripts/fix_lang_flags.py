"""
PlatformIO's ESP-IDF integration (get_app_flags -> prepare_build_envs) runs
a separate SCons env per CMake "compile group" (one per language actually
used in a target), each with correctly C-only/C++-only flags in its own
CFLAGS/CXXFLAGS. But get_app_flags then *merges every compile group's
CCFLAGS together* into one shared project-wide env, which collapses that
per-language separation: a C-only flag from the C compile group and a
C++-only flag from the CXX compile group both end up in the one shared
CCFLAGS bucket, which SCons applies to *both* compilers. cc1plus/cc1 then
reject whichever flag doesn't belong to them, as an error under -Werror.

This happens specifically because our project's own `src` component (the
only place this project mixes .c and .cpp sources in a single target) goes
through that merging codepath, while individual ESP-IDF framework
components do not (each is compiled via its own per-compile-group env, so
never shows this symptom).

Fix: once per env.Clone() (which is how espidf.py produces the env project
sources actually get compiled with - project_env = env.Clone()), move the
two known-misplaced flags out of CCFLAGS and into the correct per-language
bucket. Patched at the class level from a pre: script so it's already in
effect by the time espidf.py's own Clone() call runs later in the same
build script.
"""
Import("env")

C_ONLY_FLAGS = {"-Wno-old-style-declaration"}
CXX_ONLY_FLAGS = {"-fuse-cxa-atexit"}
MISPLACED = C_ONLY_FLAGS | CXX_ONLY_FLAGS


def _fix(target_env):
    ccflags = target_env.get("CCFLAGS", [])
    if not any(f in MISPLACED for f in ccflags):
        return
    target_env.Replace(CCFLAGS=[f for f in ccflags if f not in MISPLACED])
    target_env.Append(CFLAGS=[f for f in C_ONLY_FLAGS if f in ccflags])
    target_env.Append(CXXFLAGS=[f for f in CXX_ONLY_FLAGS if f in ccflags])


# Fix the shared env itself right now, in case anything compiles from it
# directly rather than through a later Clone().
_fix(env)

# And make every future Clone() (including espidf.py's own
# `project_env = env.Clone()`, which is what our project's .c/.cpp files
# actually get compiled with) self-heal the same way.
env_class = env.__class__
if not getattr(env_class, "_cendori_clone_patched", False):
    _orig_clone = env_class.Clone

    def _patched_clone(self, *args, **kwargs):
        new_env = _orig_clone(self, *args, **kwargs)
        _fix(new_env)
        return new_env

    env_class.Clone = _patched_clone
    env_class._cendori_clone_patched = True
