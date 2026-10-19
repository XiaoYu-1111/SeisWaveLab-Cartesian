#pragma once
struct GpuInfo;
enum class AppScreen;

// Draw the application launch workspace and return the requested route, retaining Intro0 otherwise.
AppScreen drawHomeScreen(const GpuInfo& deviceInfo,int width,int height);
// Browse the bilingual operating guide without allocating simulation resources.
AppScreen drawOperationManualScreen(int width,int height);
