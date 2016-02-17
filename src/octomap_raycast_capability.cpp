/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, SRI, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/


#include <hector_move_group_plugins/octomap_raycast_capability.h>
#include <moveit/move_group/capability_names.h>

#include <octomap/octomap.h>
#include <octomap_ros/conversions.h>

move_group::OctomapRaycastCapability::OctomapRaycastCapability():
  MoveGroupCapability("OctomapRaycastCapability")
{
}

void move_group::OctomapRaycastCapability::initialize()
{

  node_handle_.param("octomap_min_distance_to_obstacle", octo_min_distance_ ,0.05);

  //service_ = root_node_handle_.advertiseService(CLEAR_OCTOMAP_SERVICE_NAME, &OctomapAccessCapability::clearOctomap, this);

  //octomap_full_pub_ = node_handle_.advertise<octomap_msgs::Octomap>("octomap", 1, false);

  //vis_timer_ = node_handle_.createTimer(ros::Duration(1.0), &move_group::OctomapRaycastCapability::visTimerCallback, this, false);
  ros::AdvertiseServiceOptions ops=ros::AdvertiseServiceOptions::create<hector_nav_msgs::GetDistanceToObstacle>("get_distance_to_obstacle", boost::bind(&move_group::OctomapRaycastCapability::lookupServiceCallback, this,_1,_2),ros::VoidConstPtr(),&service_queue_);
  dist_lookup_srv_server_ = node_handle_.advertiseService(ops);


  service_thread_=boost::thread(boost::bind(&move_group::OctomapRaycastCapability::serviceThread,this));

  tf_ = this->context_->planning_scene_monitor_->getTFClient();
}

void move_group::OctomapRaycastCapability::serviceThread(){
    ros::Rate rate(100.0);
    while (ros::ok()){
        service_queue_.callAvailable(ros::WallDuration(1.0));
        rate.sleep();
    }
}

bool move_group::OctomapRaycastCapability::lookupServiceCallback(hector_nav_msgs::GetDistanceToObstacle::Request  &req,
                                                                 hector_nav_msgs::GetDistanceToObstacle::Response &res )
{
  //if (!map_ptr_){
    //ROS_INFO("map_server has no map yet, no lookup service available");
    //return false;
    //}
    ROS_DEBUG("Octomap distance lookup service called");
    tf::StampedTransform camera_transform;
    //boost::mutex::scoped_lock octo_lock(octomap_mutex_);
    try{
        bool useOutliers = true;
        tf_->waitForTransform("map",req.point.header.frame_id, req.point.header.stamp, ros::Duration(1.0));
        tf_->lookupTransform("map", req.point.header.frame_id, req.point.header.stamp, camera_transform);

        //const OctomapContainer<OctomapType> map_ = octomap_->getCurrentMap();
        //const OctomapType * octree_= map_.getOcTree();
        const octomap::point3d origin = octomap::pointTfToOctomap(camera_transform.getOrigin());

        tf::Point end_point = camera_transform * tf::Point(req.point.point.x, req.point.point.y, req.point.point.z);
        tf::Vector3 direction = end_point - camera_transform.getOrigin();

        const octomap::point3d directionOc = octomap::pointTfToOctomap(direction);
        std::vector<octomap::point3d> endPoints;
        std::vector<float> distances;
        int n=3;
        endPoints.resize(1);
        std::vector<octomap::point3d> directions;
        directions.push_back(directionOc);

        planning_scene_monitor::LockedPlanningSceneRO ls (context_->planning_scene_monitor_);

        // Below is inspired by
        // https://github.com/ros-planning/moveit_core/blob/jade-devel/planning_scene/src/planning_scene.cpp#L850
        // and quite hacky. There should be a better way to access the octomap (at least read-only)?
        collision_detection::CollisionWorld::ObjectConstPtr map = ls.getPlanningSceneMonitor()->getPlanningScene()->getWorld()->getObject("<octomap>");
        const shapes::OcTree* octree_shape = static_cast<const shapes::OcTree*>(map->shapes_[0].get());
        const boost::shared_ptr<const octomap::OcTree> octree_ = octree_shape->octree;

        //if(octree_->castRayMinusOne(origin,directions[0],endPoints[0],true,5.0)) {
        if(octree_->castRay(origin,directions[0],endPoints[0],true,5.0)) {
            distances.push_back(origin.distance(endPoints[0]));
        }

        if (distances.size()!=0) {
            int count_outliers;
            endPoints.resize(1+(n*2));
            // @TODO Note: distances not used anymore, has to be refactored.
            get_endpoints(distances,direction,directions,endPoints,origin,n);
            if (useOutliers) {
                double distance_threshold=0.7;
                count_outliers=0;
                for (size_t i =1;i<endPoints.size();i++){
                    ROS_DEBUG("distance to checkpoints %f",endPoints[0].distance(endPoints[i]));
                    if(endPoints[0].distance(endPoints[i])>distance_threshold) {
                        count_outliers++;
                    }
                }
                ROS_DEBUG("corner case: number of outliners: %d ",count_outliers);
            } else {
                //@TODO: Below not used, can be removed.
                /*
                Eigen::MatrixXf X;
                X.resize(endPoints.size(),2);
                Eigen::MatrixXf Y;
                Y.resize(endPoints.size(),1);
                Eigen::MatrixXf W=Eigen::MatrixXf::Zero(endPoints.size(),endPoints.size());
                int i = 0;
                for (std::vector<octomap::point3d>::const_iterator itr = endPoints.begin(); itr != endPoints.end(); itr++, i++) {
                    X(i,0) = itr->x();
                    X(i,1) = 1;
                    Y(i,0) = itr->y();
                    W(i,i) = 1;
                    if (i==(n)){
                        W(i,i) = 1000;
                    }
                }

                Eigen::Vector2f theta;
                //theta=(X.transpose()*W*X).inverse()*X.transpose()*W*Y;
                theta=((X.transpose()*W*X) + (Eigen::Matrix2f::Identity()*0.0001)).colPivHouseholderQr().solve(X.transpose()*W*Y);
                Eigen::MatrixXf P;
                P.resize(endPoints.size(),2);
                P=X*theta;

                float mse = ((Y-P).array().pow(2)).sum() / endPoints.size();
                //ROS_INFO_STREAM( "theta " << theta );
                //ROS_INFO_STREAM( "theta good ? " << (X.transpose()*W*Y).isApprox(X.transpose()*W*X * theta, 1e-3));
                //ROS_INFO_STREAM( "X " << X );
                //ROS_INFO_STREAM( "Y " << Y );
                //ROS_INFO_STREAM( "W " << W );
                //ROS_INFO_STREAM("Matrix vor inv" << X.transpose()*W*X);
                //ROS_ERROR("mse %f  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!",mse);
                */
            }

            res.distance = distances[0];

            res.end_point.header.frame_id = "map";
            res.end_point.header.stamp = ros::Time::now();
            res.end_point.point.x = endPoints[0].x();
            res.end_point.point.y = endPoints[0].y();
            res.end_point.point.z = endPoints[0].z();




            if (res.distance < octo_min_distance_) {
                res.distance = -1.0;
                ROS_WARN("Octomap GetDistanceToObstacle got distance under min_Distance -> returning -1.0");
            }

            if (useOutliers) {
                if (count_outliers>=n-1) {
                    res.distance=-1.0;
                    ROS_DEBUG("Octomap GetDistanceToObstacle Corner Case");
                }
            }
        } else {
            res.distance=-1.0;
        }

        ROS_DEBUG("Result: getDistanceObstacle_Octo: distance: %f",res.distance);
        if (m_markerPub.getNumSubscribers() > 0){
            visualization_msgs::MarkerArray marker_array;
            visualization_msgs::Marker marker;
            marker.header.stamp = req.point.header.stamp;
            marker.header.frame_id = "/map";
            marker.type = visualization_msgs::Marker::LINE_LIST;
            marker.action = visualization_msgs::Marker::ADD;
            marker.color.r= 1.0;
            marker.color.a = 1.0;
            marker.scale.x = 0.02;
            marker.ns ="";
            marker.action = visualization_msgs::Marker::ADD;
            marker.pose.orientation.w = 1.0;

            std::vector<geometry_msgs::Point> point_vector;
            for (size_t i = 0; i < endPoints.size(); ++i){
                geometry_msgs::Point tmp = octomap::pointOctomapToMsg(origin);
                point_vector.push_back(tmp);

                tmp = octomap::pointOctomapToMsg(endPoints[i]);
                point_vector.push_back(tmp);
            }
            marker.points=point_vector;
            marker_array.markers.push_back(marker);
            m_markerPub.publish(marker_array);
        }
        return true;

    }
    catch(tf::TransformException e)
    {
        ROS_ERROR("Transform failed in lookup distance service call: %s",e.what());
    }

    return false;
}

void move_group::OctomapRaycastCapability::get_endpoints(std::vector<float>& distances,
                                                         tf::Vector3 direction,
                                                         std::vector<octomap::point3d>& directions,
                                                         std::vector<octomap::point3d>& endPoints,
                                                         const octomap::point3d origin,
                                                         int n){
    tf::Vector3 zAxis(0,0,1);
    //const OctomapContainer<OctomapType> map_ = octomap_->getCurrentMap();
    //const OctomapType * octree_= map_.getOcTree();
    double toleranz=octree_->getResolution()*0.05;
    for (int i=1;i<=n;i++){
        double angle=std::atan2((octree_->getResolution()*i)+toleranz,distances[0]);

        tf::Vector3 direction_z_plus = direction.rotate(zAxis, +angle);
        tf::Vector3 direction_z_minus = direction.rotate(zAxis, -angle);

        const octomap::point3d direction_z_plus_Oc = octomap::pointTfToOctomap(direction_z_plus);
        const octomap::point3d direction_z_minus_Oc = octomap::pointTfToOctomap(direction_z_minus);

        directions.push_back(direction_z_plus_Oc);
        directions.push_back(direction_z_minus_Oc);


        if(octree_->castRay(origin,directions[2*i-1],endPoints[2*i-1],true,5.0)){
            // distances.push_back(origin.distance(endPoints[2*i-1]));
        }

        if(octree_->castRay(origin,directions[2*i],endPoints[2*i],true,5.0)){
            //distances.push_back(origin.distance(endPoints[2*i]));
        }

    }
}

/*
void move_group::OctomapRaycastCapability::visTimerCallback(const ros::TimerEvent& event)
{
  if (octomap_full_pub_.getNumSubscribers() > 0){

    moveit_msgs::PlanningScene tmp;
    moveit_msgs::PlanningSceneComponents comp;
    std::string octomap_frame_id;
    comp.components = moveit_msgs::PlanningSceneComponents::OCTOMAP;

    {
      planning_scene_monitor::LockedPlanningSceneRO ls (context_->planning_scene_monitor_);
      ls.getPlanningSceneMonitor()->getPlanningScene()->getPlanningSceneMsg(tmp, comp);
      octomap_frame_id = ls.getPlanningSceneMonitor()->getPlanningScene()->getPlanningFrame();
    }

    tmp.world.octomap.octomap.header.frame_id = octomap_frame_id;

    octomap_full_pub_.publish(tmp.world.octomap.octomap);
  }
}
*/

//bool move_group::OctomapAccessCapability::clearOctomap(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res)
//{
//  if (!context_->planning_scene_monitor_)
//  {
//    ROS_ERROR("Cannot clear octomap since planning_scene_monitor_ does not exist.");
//    return true;
//  }

//  ROS_INFO("Clearing octomap...");
//  context_->planning_scene_monitor_->clearOctomap();
//  ROS_INFO("Octomap cleared.");
//  return true;
//}


#include <class_loader/class_loader.h>
CLASS_LOADER_REGISTER_CLASS(move_group::OctomapRaycastCapability, move_group::MoveGroupCapability)
