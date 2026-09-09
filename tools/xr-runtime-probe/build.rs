#[cfg(feature = "gesture-projection")]
use std::env;
#[cfg(feature = "gesture-projection")]
use std::path::PathBuf;

#[cfg(feature = "gesture-projection")]
fn main() {
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("manifest directory"));
    let repository = manifest.join("../..");
    let include = repository.join("src/wand/include");
    let sources = [
        repository.join("src/wand/src/wand_trajectory.cpp"),
        repository.join("src/wand/src/wand_trajectory_c.cpp"),
        repository.join("src/wand/src/hp1_gesture.cpp"),
        repository.join("src/wand/src/hp1_gesture_c.cpp"),
        repository.join("src/wand/src/hp1_package_graph.cpp"),
        repository.join("src/wand/src/hp1_package_linker.cpp"),
    ];
    let headers = [
        include.join("hpvr/wand_trajectory.h"),
        include.join("hpvr/wand_trajectory_c.h"),
        include.join("hpvr/hp1_gesture.h"),
        include.join("hpvr/hp1_gesture_c.h"),
        include.join("hpvr/hp1_package_graph.h"),
        include.join("hpvr/hp1_package_linker.h"),
    ];

    for path in sources.iter().chain(headers.iter()) {
        println!("cargo:rerun-if-changed={}", path.display());
    }

    let mut build = cc::Build::new();
    build
        .cpp(true)
        .std("c++20")
        .include(include)
        .warnings(true)
        .warnings_into_errors(true);
    for source in sources {
        build.file(source);
    }

    if env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc") {
        build.flag_if_supported("/W4");
        build.flag_if_supported("/permissive-");
        build.flag_if_supported("/EHsc");
    } else {
        build.flag_if_supported("-Wall");
        build.flag_if_supported("-Wextra");
        build.flag_if_supported("-Wpedantic");
    }
    if env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("android") {
        // Keep the eventual APK self-contained instead of creating an implicit
        // packaging dependency on libc++_shared.so.
        build.cpp_link_stdlib("c++_static");
    }

    build.compile("hpvr_wand_ffi");
}

#[cfg(not(feature = "gesture-projection"))]
fn main() {}
