

# VisualStudio2022で開発する手順

- 下準備
  - cmake python3 必要

  - emsdk 必要
    - 環境変数  
      set EMSDK=c:\emsdk

- boost の用意 (例)
  > wsl wget wget https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-b2-nodocs.tar.xz  
  > wsl tar xvf boost-1.85.0-b2-nodocs.tar.xz  
  > cd boost-1.85.0  
  > wsl ./bootstrap.sh  
  > wsl ./b2 headers  
  > xcopy /S /Y boost c:\emscripten\include\boost\  

  // 環境変数
  > set EMCC_CFLAGS=-IC:/emscripten/include/

- debug 方法

  - サーバー起動
    - コマンドライン
      > %EMSDK%\\emsdk_env.bat
    - powershell 
      > & $env:EMSDK\upstream\emscripten\emrun.bat --no_browser --port 8080 .

  - ブラウザで開く
    > start http://localhost:8080/test.html

