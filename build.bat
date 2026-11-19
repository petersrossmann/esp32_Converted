@echo off
cd C:\esp-work\esp32_Converted

call C:\esp-idf\esp-idf-v5.0.9\esp-idf-v5.0.9\export.bat
python %IDF_PATH%\tools\idf.py build