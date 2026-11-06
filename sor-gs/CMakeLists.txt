add_library(sor-gs SHARED SorGS.cpp)

target_include_directories(sor-gs PRIVATE
	"${CMAKE_BINARY_DIR}/common/include"
	"${CMAKE_SOURCE_DIR}/pcsx2"
)

target_link_libraries(sor-gs PRIVATE
	PCSX2_FLAGS
	PCSX2
)
