include (AppendFlags)
append_cxx_flags (${OpenMP_CXX_FLAGS})
  
foreach (arch ${rstm_archs})
  add_executable (ssigstest${arch} test.cpp handler.S)
  # target_link_libraries (ssigstest${arch} ssigs${arch})
endforeach ()