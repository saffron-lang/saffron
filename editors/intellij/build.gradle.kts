import org.jetbrains.intellij.platform.gradle.IntelliJPlatformType

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

    // `verifyPlugin` fails outright with "No IDE resolved for verification"
    // unless this block names something, so without it the task is not a
    // passing check -- it is no check.
    //
    // Both ends of the supported range are listed, because they answer
    // different questions. 2024.2 is the sinceBuild floor claimed above: it is
    // the only thing that tests whether the LSP customization API this plugin
    // overrides actually existed that far back, rather than trusting a reading
    // of the current platform's bytecode. 2025.3 is what the plugin compiles
    // against, so it catches use of API that has since been removed or had its
    // signature changed.
    //
    // Ultimate on both, not Community: com.intellij.platform.lsp ships only in
    // Ultimate, so verifying against IC would report the plugin's entire
    // extension point as unresolved.
    pluginVerification {
        ides {
            ide(IntelliJPlatformType.IntellijIdeaUltimate, "2024.2")
            ide(IntelliJPlatformType.IntellijIdeaUltimate, "2025.3")
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
