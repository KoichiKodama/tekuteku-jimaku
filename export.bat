@echo off
if exist 字幕アプリ.zip del 字幕アプリ.zip
7z a 字幕アプリ.zip tools\ icons\ jimaku.html toolbox.html 1>NUL
