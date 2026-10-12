file(REMOVE_RECURSE
  "libknc_core.a"
  "libknc_core.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/knc_core.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
