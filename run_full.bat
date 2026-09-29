@echo off
rem ===========================================================================
rem  run_full.bat —— 一键跑完整实验
rem
rem  依次做三件事：
rem    1) 删掉 build\ 后从零重新编译 Release 与 Debug
rem    2) 两个配置各运行 N 轮（默认 3 轮）
rem    3) 调用 figs\plot_raw.py 画图（默认取两个配置各自最新的 N 轮数据）
rem
rem  用法：
rem    run_full.bat            每配置 3 轮，并从零重编译
rem    run_full.bat 5          每配置 5 轮
rem    run_full.bat 3 fast     跳过"删 build\"，用增量编译（改完代码想快速重跑时用）
rem
rem  本文件编码为 UTF-8（无 BOM）。开头把控制台切到 65001，是因为被测程序
rem  自己会调用 SetConsoleOutputCP(CP_UTF8)（见 ezlogger.cpp），统一切到 UTF-8
rem  才能让本脚本、程序日志、cmake 的中文输出都不乱码。
rem ===========================================================================
chcp 65001 >nul 2>nul
rem 让 python 的输出（含重定向到文件时）也用 UTF-8，避免日志里 UTF-8 与 GBK 混在一起
set "PYTHONIOENCODING=utf-8"
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "RUNS=%~1"
if not defined RUNS set "RUNS=3"
rem 用延迟展开做校验：参数里就算写了 & 之类，也不会被当成命令执行
echo(!RUNS!|findstr /r "^[1-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 轮数必须是正整数，例如： run_full.bat 5
    exit /b 1
)
set "CLEAN=1"
if /i "%~2"=="fast" set "CLEAN="

echo.
echo ===========================================================
echo  完整实验：重新编译 -^> 各跑 !RUNS! 轮 -^> 画图
echo  工程目录：%CD%
echo  开始时间：%DATE% %TIME%
if defined CLEAN (echo  编译方式：从零编译（删掉 build\）) else (echo  编译方式：增量编译（fast）)
echo ===========================================================

echo.
echo [1/3] 编译 Release 与 Debug
if defined CLEAN (
    if exist "build" rmdir /s /q "build"
    if exist "build" (
        echo [错误] 删不掉 build\，可能被其它程序占用，关掉再试（或改用 fast）
        exit /b 1
    )
)
echo   -- 配置 Release ...
cmake --preset "release without pdb" >nul
if errorlevel 1 ( echo [错误] Release 配置失败 & exit /b 1 )
echo   -- 编译 Release ...
cmake --build --preset "release without pdb"
if errorlevel 1 ( echo [错误] Release 编译失败 & exit /b 1 )
echo   -- 配置 Debug ...
cmake --preset debug >nul
if errorlevel 1 ( echo [错误] Debug 配置失败 & exit /b 1 )
echo   -- 编译 Debug ...
cmake --build --preset debug
if errorlevel 1 ( echo [错误] Debug 编译失败 & exit /b 1 )
if not exist "release\main.exe"    ( echo [错误] 没有生成 release\main.exe & exit /b 1 )
if not exist "debug\main\main.exe" ( echo [错误] 没有生成 debug\main\main.exe & exit /b 1 )
echo   编译完成。

echo.
echo [2/3] 运行实验：每个配置 !RUNS! 轮，每轮约 40~45 秒
rem 注意：循环体里的时间必须用延迟展开的 TIME 变量（即叹号包裹的写法）。若写普通展开，整块在进入循环时
rem 就展开成同一个值，几轮会显示出一样的时间戳。
for /L %%i in (1,1,!RUNS!) do (
    echo   --- Release  第 %%i/!RUNS! 轮   !TIME! ---
    "release\main.exe"
    if errorlevel 1 ( echo [错误] Release 第 %%i 轮退出码非 0，已中止 & exit /b 1 )
)
for /L %%i in (1,1,!RUNS!) do (
    echo   --- Debug    第 %%i/!RUNS! 轮   !TIME! ---
    "debug\main\main.exe"
    if errorlevel 1 ( echo [错误] Debug 第 %%i 轮退出码非 0，已中止 & exit /b 1 )
)

echo.
echo [3/3] 画图：每个配置取最新 !RUNS! 轮
where python >nul 2>nul
if errorlevel 1 (
    echo   [跳过] 没找到 python。装好 Python + matplotlib + numpy 后单独运行：
    echo          python figs\plot_raw.py --runs !RUNS!
) else (
    python "figs\plot_raw.py" --runs !RUNS!
    if errorlevel 1 echo   [注意] 画图失败，请看上面的报错；实验数据已产出，可稍后单独重画
)

echo.
echo ===========================================================
echo  全部完成   结束时间：%DATE% %TIME%
echo  原始数据：release\raw_data\  与  debug\main\raw_data\
echo  报告日志：release\log\      与  debug\main\log\
echo  图片：    figs\
echo ===========================================================
endlocal
pause