plugins {
    id("com.android.application")
}

val portAbis = (project.findProperty("portAbis") as String? ?: "arm64-v8a")
    .split(",").map { it.trim() }.filter { it.isNotEmpty() }
val sdlSource = file(project.findProperty("sdl2SourceDir") as String?
    ?: rootProject.file("third_party/SDL").path)
val portSource = project.findProperty("portSourceDir") as String?
val notices = layout.buildDirectory.dir("generated/notices")
val stageNotices by tasks.registering(Sync::class) {
    from(rootProject.file("../src/audio/COPYING.Nuked-OPL3"))
    from(rootProject.file("../../LICENSE")) {
        rename { "CyberMage-LICENSE.txt" } /* AGP drops resources named LICENSE. */
    }
    from(rootProject.file("../../ATTRIBUTIONS.md"))
    from(sdlSource.resolve("LICENSE.txt")) {
        rename { "SDL2-LICENSE.txt" }
    }
    into(notices)
}

android {
    namespace = "com.jshield.cybermage"
    compileSdk = 36
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "com.jshield.cybermage"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
        ndk {
            abiFilters += portAbis
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                    "-DSDL2_SOURCE_DIR=${sdlSource.path}",
                    "-DCMAKE_BUILD_TYPE=MinSizeRel",
                    "-DCMAKE_JOB_POOLS=cm_compile=2",
                    "-DCMAKE_JOB_POOL_COMPILE=cm_compile",
                )
                if (portSource != null) arguments += "-DPORT_SOURCE_DIR=$portSource"
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
            buildStagingDirectory = rootProject.layout.projectDirectory.dir("build/cxx").asFile
        }
    }
    sourceSets {
        getByName("main") {
            java.srcDir(sdlSource.resolve("android-project/app/src/main/java"))
            resources.srcDir(notices)
        }
    }
}

tasks.named("preBuild") {
    dependsOn(stageNotices)
}
