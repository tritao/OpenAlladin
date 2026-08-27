@echo off
setlocal
set ROOT_DIR=%~dp0..\..
if "%GHIDRA_INSTALL_DIR%"=="" set GHIDRA_INSTALL_DIR=%ROOT_DIR%\.tools\ghidra-12.1.3
if not exist "%GHIDRA_INSTALL_DIR%\support\pyghidraRun.bat" (
  echo Ghidra is not installed at %GHIDRA_INSTALL_DIR%
  echo Run: genie setup
  exit /b 1
)
set OPENALADDIN_ROOT=%ROOT_DIR%
if exist "%ROOT_DIR%\.tools\venv\Scripts" set PATH=%ROOT_DIR%\.tools\venv\Scripts;%PATH%
set PYTHONPATH=%ROOT_DIR%;%PYTHONPATH%
call "%GHIDRA_INSTALL_DIR%\support\pyghidraRun.bat" %*
