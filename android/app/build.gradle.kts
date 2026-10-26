plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "ru.kadrolov.app"
  compileSdk = 35
  buildToolsVersion = "35.0.0"
  defaultConfig {
    applicationId = "ru.kadrolov.app"
    minSdk = 29
    targetSdk = 35
    versionCode = 1
    versionName = "0.1.0"
    testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  kotlinOptions { jvmTarget = "17" }
  buildTypes { release { isMinifyEnabled = false } }
  lint { abortOnError = true }
}

dependencies {
  implementation("cz.adaptech.tesseract4android:tesseract4android:4.9.0")
  testImplementation("junit:junit:4.13.2")
  androidTestImplementation("androidx.test:runner:1.6.2")
  androidTestImplementation("androidx.test.ext:junit:1.2.1")
  androidTestImplementation("androidx.test.uiautomator:uiautomator:2.3.0")
}
