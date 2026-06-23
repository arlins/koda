C++17-like Classes/Libs implementation for C++11/C++14
Use run fetch_libs script to download all libs. 
Edit libs-config.json for more Libs.


Libs:
C++11-std::any：https://github.com/nonstd-lite/any-lite
C++11-std::optional: https://github.com/nonstd-lite/optional-lite
C++11-std::variant https://github.com/nonstd-lite/variant-lite
C++11-std::string_view：https://github.com/nonstd-lite/string-view-lite
C++11-std::filesystem：https://github.com/gulrak/filesystem


libs-config.json

// fetch from master
"url": "https://github.com/nonstd-lite/any-lite"

// fetch from tag 0.3.0
"url": "https://github.com/nonstd-lite/any-lite@0.3.0"

// fetch from zip
"url": "https://github.com/nonstd-lite/any-lite/archive/refs/heads/master.zip"