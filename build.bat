@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /EHsc /std:c++17 /utf-8 /Fe:cloc.exe main.cpp
cl /nologo /EHsc /std:c++17 /utf-8 /Fe:cloc-server.exe server.cpp /link Ws2_32.lib
