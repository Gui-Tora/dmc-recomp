$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$build=Join-Path $repo 'analysis/local/symtabfirst/build'
$out=Join-Path $repo 'analysis/local/p40'
$vc='C:\Program Files\Microsoft Visual Studio\2022\Community'
$envLines=& cmd.exe /c "`"$vc\Common7\Tools\VsDevCmd.bat`" -no_logo -arch=x64 >nul && set"
foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$lib=Join-Path $build 'upstream/ps2xRuntime/Debug/ps2_runtime.lib'
$baseline=Join-Path $out 'ps2_runtime.entry.lib'
if(Test-Path -LiteralPath $baseline){throw 'Entry library backup already exists; inspect before repeating'}
Copy-Item -LiteralPath $lib -Destination $baseline
$members=@(& lib.exe /nologo /list $lib)
$members | Set-Content (Join-Path $out 'library_members_before.txt')
$objects=@()
$remove=@()
foreach($unit in @('ps2_memory','gs_frontend')){
    $old=@($members | Where-Object { $_ -match "(^|[\\/])$unit\.obj$" })
    if($old.Count -ne 1){throw "Expected one $unit member"}
    $remove+="/REMOVE:$($old[0])"
    $obj=Join-Path $build "upstream/ps2xRuntime/ps2_runtime.dir/Debug/$unit.obj"
    $source=if($unit -eq 'gs_frontend'){'gs/gs_frontend.cpp'}else{'ps2_memory.cpp'}
    $inc=@('vendor/PS2Recomp/ps2xRuntime/include','vendor/PS2Recomp/ps2xRuntime/src/lib/Kernel','analysis/local/symtabfirst/build/_deps/raylib-src/src','analysis/local/symtabfirst/build/_deps/raylib-src/src/external/glfw/include','vendor/PS2Recomp/ps2xIOP/include','analysis/local/symtabfirst/build/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/include')
    $clargs=@('/nologo','/c','/std:c++20','/EHsc','/MDd','/Od','/Zi','/FS','/DWIN32','/D_WINDOWS','/D_DEBUG','/DPS2_RUNTIME_LOGS=1','/DPS2X_ENABLE_IOP_RPC_TRACE=1','/DPS2X_HAS_FFMPEG=1','/DPLATFORM_DESKTOP','/DGRAPHICS_API_OPENGL_33','/DPS2X_IOP_ENABLE_PLUGINS=0',"/Fo$obj",('/Fd'+(Join-Path $out 'compiler.pdb')))
    foreach($i in $inc){$clargs+=('/I'+(Join-Path $repo $i))}
    $clargs+=(Join-Path $repo "vendor/PS2Recomp/ps2xRuntime/src/lib/$source")
    & cl.exe @clargs
    if($LASTEXITCODE -ne 0){throw "Compile failed $unit"}
    $objects+=$obj
}
$without=Join-Path $out 'without.lib'
$updated=Join-Path $out 'updated.lib'
& lib.exe /nologo "/OUT:$without" @remove $baseline
if($LASTEXITCODE -ne 0){throw 'Remove exact members failed'}
& lib.exe /nologo "/OUT:$updated" $without @objects
if($LASTEXITCODE -ne 0){throw 'Add replacement members failed'}
$after=@(& lib.exe /nologo /list $updated)
$after | Set-Content (Join-Path $out 'library_members_after.txt')
foreach($unit in @('ps2_memory','gs_frontend','MPEG')){if(@($after | Where-Object {$_ -match "(^|[\\/])$unit\.obj$"}).Count -ne 1){throw "Duplicate/missing member $unit"}}
if($after.Count -ne $members.Count){throw 'Archive member count changed'}
Copy-Item -LiteralPath $updated -Destination $lib
& "$vc\MSBuild\Current\Bin\MSBuild.exe" (Join-Path $build 'upstream/ps2xRuntime/ps2EntryRunner.vcxproj') /t:_BuildLinkAction /p:Configuration=Debug /p:Platform=x64 /v:minimal
if($LASTEXITCODE -ne 0){throw 'Link failed'}
Write-Output 'P40_BUILD_SUCCESS: two runtime sources, archive count preserved, link only'
