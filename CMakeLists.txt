cmake_minimum_required(VERSION 2.8.3)
project(hector_move_group_capabilities)

set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_BUILD_TYPE Debug)

find_package(catkin REQUIRED COMPONENTS
  geometry_msgs
  hector_nav_msgs
  moveit_msgs
  moveit_ros_move_group
  octomap_msgs
  octomap_ros
  pluginlib
  roscpp
  sensor_msgs
  tf
)

catkin_package(
  INCLUDE_DIRS include
#  LIBRARIES vigir_move_group
#  CATKIN_DEPENDS cmake_modules geometry_msgs moveit_msgs moveit_ros_move_group pluginlib roscpp
#  DEPENDS system_lib
)


include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

add_library(hector_move_group_capabilities
  #src/continuous_replanning_capability.cpp
  #src/manipulation_action_capability.cpp
  src/octomap_raycast_capability.cpp
  #src/octomap_management_capability.cpp
  #src/robot_state_retrieval_capability.cpp
  )

target_link_libraries(hector_move_group_capabilities ${catkin_LIBRARIES} ${Boost_LIBRARIES})

add_dependencies(hector_move_group_capabilities ${catkin_EXPORTED_TARGETS})

install(TARGETS hector_move_group_capabilities
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

install(DIRECTORY include/ DESTINATION include)

install(FILES
  hector_move_group_plugins_description.xml
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
