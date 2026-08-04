plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.2.0"
    id("org.jetbrains.intellij.platform") version "2.6.0"
}

group = "dev.saffron"
version = "0.1.0"

repositories {
    mavenCentral()
    intellijPlatform {
        defaultRepositories()
    }
}

dependencies {
    intellijPlatform {
        intellijIdeaUltimate("2025.3")
        bundledPlugin("org.jetbrains.plugins.textmate")
    }
}

kotlin {
    jvmToolchain(21)
}

intellijPlatform {
    pluginConfiguration {
        name = "Saffron"

        // Without this the Gradle plugin derives since-build from the platform it
        // compiled against (2025.3 -> since-build 253), which pins a sideloaded
        // build to that release and newer even though nothing here needs it. The
        // real floor is the LSP customization API used in the descriptor
        // (LspFormattingSupport as an overridable val), which is 2024.2.
        //
        // untilBuild is left unset on purpose: a fixed upper bound makes the
        // plugin refuse to load after the next IDE upgrade, which for a
        // sideloaded plugin means silently losing language support until someone
        // rebuilds it.
        ideaVersion {
            sinceBuild = "242"
            untilBuild = provider { null }
        }
    }
}

tasks {
    prepareSandbox {
        from("src/main/resources/textmate") {
            into("saffron-intellij/textmate")
        }
    }
}
