plugins {
    id("com.android.application") version "8.13.2" apply false
}

allprojects {
    layout.buildDirectory.set(rootProject.layout.projectDirectory.dir("build/${project.name}"))
}
