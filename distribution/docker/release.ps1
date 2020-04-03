# Build Script...
$ErrorActionPreference="Stop"
$BuildMode="Release"


Write-Host 'Building Tinyphone!'

cd C:\Code

git config --global url.https://github.com/.insteadOf git@github.com:
git clone --recurse-submodules -j8 https://github.com/voiceip/tinyphone.git
cd C:\Code\tinyphone\
# git checkout docker

# git apply C:\Build\fk.patch


cmd /c subst E: C:\Code\tinyphone


cd P:\lib\curl\
.\buildconf.bat
cd P:\lib\curl\winbuild

nmake /f Makefile.vc mode=dll VC=15 DEBUG=no

cd P:\lib\curl\builds
cmd /c MKLINK /D P:\lib\curl\builds\libcurl-vc-x86-release-dll-ipv6-sspi-winssl P:\lib\curl\builds\libcurl-vc15-x86-release-dll-ipv6-sspi-winssl
ls P:\lib\curl\builds
cmd /c .\libcurl-vc15-x86-release-dll-ipv6-sspi-winssl\bin\curl.exe https://wttr.in/bangalore


#great 


#G729
cd P:\lib\bcg729\build\
cmake ..
msbuild /m bcg729.sln /p:Configuration=$BuildMode /p:Platform=Win32


cd P:\lib\cryptopp
msbuild /m cryptlib.vcxproj /p:Configuration=$BuildMode /p:Platform=Win32 /p:PlatformToolset=v140_xp

$wc = New-Object net.webclient; $wc.Downloadfile("https://download.steinberg.net/sdk_downloads/asiosdk_2.3.3_2019-06-14.zip", "P:\lib\portaudio\src\hostapi\asio\asiosdk_2.3.3_2019-06-14.zip")
cd P:\lib\portaudio\src\hostapi\asio
unzip asiosdk_2.3.3_2019-06-14.zip
mv asiosdk_2.3.3_2019-06-14 ASIOSDK
cd P:\lib\portaudio\build\msvc
msbuild /m portaudio.sln /p:Configuration=$BuildMode /p:Platform=Win32

cd P:\lib\pjproject
msbuild /m pjproject-vs14.sln -target:libpjproject:Rebuild /p:Configuration=$BuildMode-Static /p:Platform=Win32

cd P:\lib\statsd-cpp
cmake .
msbuild /m statsd-cpp.vcxproj /p:Configuration=$BuildMode /p:Platform=Win32



#ls P:\lib\curl\builds\libcurl-vc-x86-release-dll-ipv6-sspi-winssl
cd E:\tinyphone
#msbuild /m tinyphone.sln -target:tinyphone /p:Configuration=$BuildMode /p:Platform=x86
#msbuild /m tinyphone.sln -target:tinyphone:Rebuild /p:Configuration=$BuildMode /p:Platform=x86
msbuild /m tinyphone.sln /p:Configuration=$BuildMode /p:Platform=x86


#git diff --exit-code stampver.inf
