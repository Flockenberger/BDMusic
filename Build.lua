-- premake5.lua
workspace "BDMusic"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "BDMusic"

   -- Workspace-wide build options for MSVC
   filter "system:windows"
      buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

OutputDir = "%{cfg.system}-%{cfg.architecture}/%{cfg.buildcfg}"

include "Modules/BDMusic/BDMusicBuild.lua"
