param([switch]$Mpeg)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$build=Join-Path $repo 'analysis/local/symtabfirst/build'
$vc= 'C:\Program Files\Microsoft Visual Studio\2022\Community'
# Use an explicit cmd invocation only for compiler environment setup, never deletion.
$envLines=& cmd.exe /c "`"$vc\Common7\Tools\VsDevCmd.bat`" -no_logo -arch=x64 >nul && set"
foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$obj=Join-Path $build 'upstream/ps2xRuntime/ps2EntryRunner.dir/Debug/dmc_overrides.obj'
$includes=@('analysis/local/symtabfirst/generated','vendor/PS2Recomp/ps2xRuntime/include','vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel','analysis/local/symtabfirst/build/_deps/raylib-src/src','analysis/local/symtabfirst/build/_deps/raylib-src/src/external/glfw/include','vendor/PS2Recomp/ps2xIOP/include')
$clargs=@('/nologo','/c','/std:c++20','/EHsc','/MDd','/Od','/Zi','/FS','/DWIN32','/D_WINDOWS','/D_DEBUG','/DPS2_RUNTIME_LOGS=1','/DPS2X_ENABLE_IOP_RPC_TRACE=1','/DPLATFORM_DESKTOP','/DGRAPHICS_API_OPENGL_33','/DPS2X_IOP_ENABLE_PLUGINS=0',"/Fo$obj",('/Fd'+(Join-Path $build 'p311_compiler.pdb')))
foreach($inc in $includes){$clargs+=('/I'+(Join-Path $repo $inc))}
if($Mpeg){
    $mpegObj=Join-Path $build 'upstream/ps2xRuntime/ps2_runtime.dir/Debug/MPEG.obj'
    $clargs=$clargs | Where-Object {$_ -notlike '/Fo*'}
    $clargs+=@("/Fo$mpegObj",'/DPS2X_HAS_FFMPEG=1',('/I'+(Join-Path $build 'ThirdParty/ffmpeg-prefix/src/ffmpeg_external/include')),(Join-Path $repo 'vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp'))
}else{$clargs+=(Join-Path $repo 'runtime/dmc_overrides.cpp')}
& cl.exe @clargs
if($LASTEXITCODE -ne 0){throw 'Single file compilation failed'}
if($Mpeg){
    $lib=Join-Path $build 'upstream/ps2xRuntime/Debug/ps2_runtime.lib'
    $backup=Join-Path $repo 'analysis/local/p311/ps2_runtime.before_mpeg_probe.lib'
    if(!(Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $lib -Destination $backup}
    $updated=Join-Path $repo 'analysis/local/p311/ps2_runtime.updated.lib'
    $without=Join-Path $repo 'analysis/local/p311/ps2_runtime.without_mpeg.lib'
    & lib.exe /nologo "/OUT:$without" '/REMOVE:ps2_runtime.dir\Debug\MPEG.obj' $backup
    if($LASTEXITCODE -ne 0){throw 'Archive member removal failed'}
    & lib.exe /nologo "/OUT:$updated" $without $mpegObj
    if($LASTEXITCODE -ne 0){throw 'Library member replacement failed'}
    $members=@(& lib.exe /nologo /list $updated | Where-Object {$_ -match 'MPEG\.obj$'})
    if($members.Count -ne 1){throw 'Expected exactly one MPEG.obj archive member'}
    Copy-Item -LiteralPath $updated -Destination $lib
}
& "$vc\MSBuild\Current\Bin\MSBuild.exe" (Join-Path $build 'upstream/ps2xRuntime/ps2EntryRunner.vcxproj') /t:_BuildLinkAction /p:Configuration=Debug /p:Platform=x64 /v:minimal
if($LASTEXITCODE -ne 0){throw 'Link action failed'}
