file(REMOVE_RECURSE
  "libknc.pdb"
  "libknc.so"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/knc_shared.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
