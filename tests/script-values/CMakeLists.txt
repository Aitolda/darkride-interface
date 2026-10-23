
# Declare dependencies
macro (setup_testcase_dependencies)
  link_hifi_libraries(shared test-utils)
  package_libraries_for_deployment()
endmacro ()

# Script: PlainScriptValueTests compares the plain value type with QtScript's QScriptValue
setup_hifi_testcase(Script)
