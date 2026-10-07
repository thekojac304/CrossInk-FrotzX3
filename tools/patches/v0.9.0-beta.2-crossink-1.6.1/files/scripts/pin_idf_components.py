"""
PlatformIO pre-build script: pin floating ESP-IDF managed components.

Problem:
  Arduino-ESP32 3.3.7 declares `espressif/mdns: ^1.2.3` in its own
  idf_component.yml. When pioarduino rebuilds the Arduino IDF libs for
  `custom_sdkconfig`, the IDF component manager resolves that range to the
  newest 1.x release at build time (1.11.3 on an older checkout, 1.14.0 on a
  fresh clone), so two builds of the same commit can compile different mdns
  sources. pioarduino deletes `dependencies.lock` after the lib build and
  `custom_component_add` cannot override an entry that already exists, so a
  lock file cannot be used to pin it.

Fix:
  Add an exact-version constraint to the project's `.dummy/idf_component.yml`
  (the manifest of the IDF project pioarduino builds the libs from). The
  component manager intersects it with the framework's `^1.2.3` range.
  pioarduino only creates `.dummy` when it is missing, so it is seeded here
  from the platform's own template first.

Applied idempotently — safe to run on every build.
"""

Import("env")
import os
import re
import shutil

# Registry name -> exact version. Versions are the ones the verified clean
# build resolved; bump deliberately and rebuild from a clean tree.
PINNED_COMPONENTS = {
    "espressif/mdns": "1.14.0",
}

BEGIN = "  # >>> pinned by scripts/pin_idf_components.py\n"
END = "  # <<< pinned by scripts/pin_idf_components.py\n"


def pin_idf_components(env):
    if not env.GetProjectOption("custom_sdkconfig", None):
        return  # no IDF lib rebuild for this env

    dummy_dir = os.path.join(env["PROJECT_DIR"], ".dummy")
    if not os.path.isdir(dummy_dir):
        template = os.path.join(env.PioPlatform().get_dir(), "builder", "build_lib")
        shutil.copytree(template, dummy_dir)

    manifest = os.path.join(dummy_dir, "idf_component.yml")
    with open(manifest, "r", encoding="utf-8") as f:
        original = f.read()

    block = BEGIN + "".join(
        f'  {name}:\n    version: "=={version}"\n'
        for name, version in PINNED_COMPONENTS.items()
    ) + END

    # Drop any block from a previous run, then re-insert under `dependencies:`.
    content = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), "", original, flags=re.S)
    updated, count = re.subn(r"^dependencies:[ \t]*\n", lambda m: m.group(0) + block,
                             content, count=1, flags=re.M)
    if count != 1:
        raise RuntimeError(f"pin_idf_components.py: no 'dependencies:' in {manifest}")

    if updated != original:
        with open(manifest, "w", encoding="utf-8", newline="\n") as f:
            f.write(updated)
        print("Pinned ESP-IDF components: " + ", ".join(
            f"{n}=={v}" for n, v in PINNED_COMPONENTS.items()))


pin_idf_components(env)
