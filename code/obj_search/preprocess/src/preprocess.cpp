/**
 * @file   planeTest.cpp
 * @author Michal Staniaszek <michalst@kth.se>
 * @date   Mon Mar  9 14:06:36 2015
 * 
 * @brief  Some prototype code for doing preprocessing on point clouds.
 */
#include "preprocess.hpp"

namespace objsearch {
    namespace preprocessing {

	/** 
	 * Constructor for the preprocessing node class. Uses parameters
	 * received from the parameter server to set up internal attributes.
	 * Once the object has been constructed, it calls functions to process
	 * the given point cloud.
	 * 
	 * @param argc Number of input arguments from main
	 * @param argv Values of input arguments from main
	 */
	PreprocessRoom::PreprocessRoom(int argc, char* argv[]){
	    ros::init(argc, argv, "preprocess");
	    ros::NodeHandle handle;

	    // Retrieve the directory containing the cloud to be processed
	    ROSUtil::getParam(handle, "/preprocess/cloud_dir", cloudDir);

	    // Construct the filenames for the XML file containing data, and the
	    // merged cloud
	    roomXML = std::string(SysUtil::fullDirPath(cloudDir)) + "room.xml";
	    roomCloud = std::string(SysUtil::fullDirPath(cloudDir)) + "complete_cloud.pcd";
    
	    ROSUtil::getParam(handle, "/obj_search/raw_data_dir", dataPath);

	    // If the given cloud file corresponds to a file in the raw data
	    // directory, extract the remaining directories in the path of the
	    // file so that the data can be put into the output directory with
	    // the same path. e.g. if raw_data_dir is set to /home/user/data and
	    // the input cloud is in /home/user/data/sets/set1/, then datasubdir
	    // will be /sets/set1.
	    if (roomCloud.compare(0, dataPath.size(), dataPath) == 0){
		dataSubDir = SysUtil::trimPath(std::string(roomCloud, dataPath.size()), 1);
	    }

	    ROSUtil::getParam(handle, "/preprocess/output_dir", outDir);
	    // If output is not specified, set the output directory to be the processed
	    // data directory specified by the global parameters.
	    if (std::string("NULL").compare(outDir) == 0) {
		ROSUtil::getParam(handle, "/obj_search/processed_data_dir", outDir);
	    }

	    // The output path for processed clouds is the subdirectory combined
	    // with the top level output directory. If dataSubDir is not
	    // initialised, then clouds are simply output to the top level
	    // output directory
	    outPath = SysUtil::combinePaths(outDir, dataSubDir);

	    ROSUtil::getParam(handle, "/preprocess/RANSAC_distance_threshold", ransacDistanceThresh);
	    ROSUtil::getParam(handle, "/preprocess/RANSAC_iterations", ransacIterations);
	    ROSUtil::getParam(handle, "/preprocess/planes_to_extract", planesToExtract);
	    ROSUtil::getParam(handle, "/preprocess/floor_offset", floorOffset);
	    ROSUtil::getParam(handle, "/preprocess/ceiling_offset", ceilingOffset);
	    ROSUtil::getParam(handle, "/obj_search/floor_z", floorZ);
	    ROSUtil::getParam(handle, "/obj_search/ceiling_z", ceilingZ);

	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr workingCloud;
	    tf::StampedTransform roomRotation;

	    ROS_INFO("Starting load");
	    loadRoom(workingCloud, roomRotation, roomXML);
	    ROS_INFO("Finished load");
	    ROS_INFO("Cloud size outside load %d", (int)workingCloud->size());
	    transformAndRemoveFloorCeiling(workingCloud, roomRotation);
	    extractPlanes(workingCloud);
	}

	/** 
	 * Load a room from the directory of interest, using the given XML file
	 * to extract information about the rotation of the room. The cloud data
	 * is loaded into the cloud pointer passed to the function, the
	 * transformation data is put into the StampedTransform given.
	 * 
	 * @param cloud This pointer to a cloud will be populated with the
	 * merged cloud representing the room.
	 * @param roomTransform This object will be populated with the
	 * transformation from the global frame to the local cloud frame.
	 * @param fileXMLPath Path to the XML file which holds information about
	 * the room to process
	 */
	void PreprocessRoom::loadRoom(pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud, // needs to be a reference to allow modification. Weird pointer type
				      tf::StampedTransform& roomTransform,
				      std::string fileXMLPath){
	    SimpleXMLParser<pcl::PointXYZRGB> parser;
	    ROS_INFO("loadroom: Starting load");
	    SimpleXMLParser<pcl::PointXYZRGB>::RoomData roomData = parser.loadRoomFromXML(fileXMLPath);
	    ROS_INFO("loadroom: Load complete.");
	    
	    // if (pcl::io::loadPCDFile<pcl::PointXYZRGB>(roomCloud, *cloud) != -1){
	    // 	std::cout << "Loaded cloud from " << roomCloud.c_str() << std::endl;
	    // } else {
	    // 	std::cout << "Could not load cloud from " << roomCloud.c_str() << std::endl;
	    // 	exit(1);
	    // }

	    cloud = roomData.completeRoomCloud->makeShared();
	    ROS_INFO("loadroom: Cloud size %d", (int)cloud->size());
	    roomTransform = roomData.vIntermediateRoomCloudTransforms[0];
	}

	/** 
	 * The cloud we receive is translated and rotated relative to the global
	 * frame. It is possible to extract this from the gathered data using
	 * the XML provided with each room indicating the rotation of
	 * intermediate clouds. We use the rotation of the first intermediate
	 * cloud to translate and rotate the merged cloud into its actual
	 * position, and then threshold the z-axis values according to the
	 * height of the floor and ceiling (plus some offset) to remove points
	 * which make up those structures.
	 *
	 * @param cloud Cloud to transform and remove the floor and ceiling
	 * from.
	 * @param roomTransform The transform needed to put the cloud into
	 * its position relative to the global reference frame. Ideally
	 * extracted from the XML data for the room and its intermediate clouds.
	 */
	void PreprocessRoom::transformAndRemoveFloorCeiling(pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
							    const tf::StampedTransform& roomTransform){
//	    tf::StampedTransform roomRotation = roomData.vIntermediateRoomCloudTransforms[0];
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformedCloud(new pcl::PointCloud<pcl::PointXYZRGB>);

	    // This is the point at which the camera was while taking the images.
	    tf::Vector3 origin = roomTransform.getOrigin();
	    std::cout << origin.getX() << ", " << origin.getY() << ", " << origin.getZ() << std::endl;
	    // Transform the cloud according to the given transform. After this
	    // point the x-y axis should be aligned with the floor, and the
	    // z-axis should point upwards.
	    pcl_ros::transformPointCloud(*cloud, *transformedCloud, roomTransform);

	    // pcl::PointXYZRGB min;
	    // pcl::PointXYZRGB max;
	    // pcl::getMinMax3D(*transformedCloud, min, max);

	    // ROS_INFO("min: %f, %f, %f", min.x, min.y, min.z);
	    // ROS_INFO("max: %f, %f, %f", max.x, max.y, max.z);

	    // Use a passthrough filter to remove points beyond a specific
	    // threshold - the floor position plus an offset, and the ceiling
	    // position minus an offset. Positive z points towards the ceiling.
	    // The floor and ceiling heights are known, but we want to remove
	    // the floors and ceilings, so a little bit is added to make sure that happens.
	    pcl::PassThrough<pcl::PointXYZRGB> pass;
	    pass.setInputCloud(transformedCloud);
	    pass.setFilterFieldName("z");
	    // a little bit above the floor, and a little below the ceiling are our limits
	    pass.setFilterLimits(floorZ + floorOffset, ceilingZ - ceilingOffset);
	    // filter the values outside the thresholds and put the rest of the
	    // points back into the cloud.
	    pass.filter(*cloud); 

	    pcl::PCDWriter writer;
	    ROS_INFO("Writing transformed cloud...");
	    writer.write<pcl::PointXYZRGB>(SysUtil::fullDirPath(cloudDir) + "transformedRoom.pcd", *transformedCloud, true);
	    ROS_INFO("Done");
	    ROS_INFO("Writing trimmed cloud...");
	    writer.write<pcl::PointXYZRGB>(SysUtil::fullDirPath(cloudDir) + "trimmedRoom.pcd", *cloud, true);
	    ROS_INFO("Done");
	}

	/** 
	 * Once the room has been stripped of its floor and ceiling, remove
	 * other planes from the room. We aim to remove walls and other large
	 * flat surfaces from which we do not want to extract features. This
	 * process is done using a basic RANSAC procedure.
	 *
	 * @param cloud Pointer to the cloud from which planes are to be extracted.
	 * 
	 */
	void PreprocessRoom::extractPlanes(pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud){
	    ROS_INFO("Extracting planes.");
	    ROS_INFO("Number of points in original cloud: %d", (int)cloud->size());

	    pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
	    pcl::PointIndices::Ptr inliers(new pcl::PointIndices);

	    // Set up the segmentation to extract planes from the cloud using
	    // RANSAC with the parameters specified using the launch file
	    pcl::SACSegmentation<pcl::PointXYZRGB> seg;
	    seg.setOptimizeCoefficients(true);
	    seg.setModelType(pcl::SACMODEL_PLANE);
	    seg.setMethodType(pcl::SAC_RANSAC);
	    seg.setDistanceThreshold(ransacDistanceThresh);
	    seg.setMaxIterations(ransacIterations);

	    pcl::ExtractIndices<pcl::PointXYZRGB> extract;
	    // Points will be removed from this cloud - at the end of the process it
	    // will contain all the points which were not extracted by the segmentation
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr intermediateCloud (cloud);
	    // At each stage, the inliers of the plane model will be extracted to this
	    // cloud
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr extractedPlane (new pcl::PointCloud<pcl::PointXYZRGB>);
	    // All of the points that are extracted throughout the process will end up
	    // in this cloud
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr allPlanes (new pcl::PointCloud<pcl::PointXYZRGB>);
	    // The points which are not inliers to the plane will be placed into this
	    // cloud and then swapped into intermediateCloud
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr remainingPoints (new pcl::PointCloud<pcl::PointXYZRGB>);

	    // create the directory for output if it has not already been created
	    if (!SysUtil::makeDirs(outPath)){
		std::cout << "Could not write point clouds to output directory." << std::endl;
		perror("Error message");
		exit(1);
	    }

	    pcl::PCDWriter writer;
	    ROS_INFO("Starting plane extraction.");
	    for (int i = 0; i < planesToExtract; i++) {
		ROS_INFO("Extracting plane %d", i + 1);
		seg.setInputCloud(intermediateCloud);
		seg.segment(*inliers, *coefficients);

		if (inliers->indices.size () == 0) {
		    std::cerr << "Could not estimate a planar model for the given dataset." << std::endl;
		    break;
		}

		ROS_INFO("Size of intermediate cloud: %d", (int)intermediateCloud->size());
		
		// Extract the inliers
		extract.setInputCloud(intermediateCloud);
		extract.setIndices(inliers);
		extract.setNegative(false); // Extract the points which are inliers
		extract.filter(*extractedPlane);
		ROS_INFO("Number of points on extracted plane: %d", (int)extractedPlane->size());
		ROS_INFO("Number of points on all planes: %d", (int)allPlanes->size());
		*allPlanes += *extractedPlane; // Add the extracted inliers to the cloud of all planes
		ROS_INFO("Number of points after adding new plane: %d", (int)allPlanes->size());

		writer.write<pcl::PointXYZRGB>(SysUtil::fullDirPath(outPath) + "extractedPlane_" + std::to_string(i) +".pcd",
					       *allPlanes, true);
		// Extract non-inliers
		extract.setNegative(true); // Extract the points which are not inliers
		extract.filter(*remainingPoints);
		intermediateCloud.swap(remainingPoints);
		ROS_INFO("Intermediate cloud after swapping in non-inlier points: %d", (int)intermediateCloud->size());
	    }

	    ROS_INFO("All planes size after loop: %d", (int)allPlanes->size());
	    ROS_INFO("Remaining points size after loop: %d", (int)intermediateCloud->size());
	    
	    std::cout << "Outputting results to: " << outPath << std::endl;

	    // Write the extracted planes and the remaining points to separate files
	    writer.write<pcl::PointXYZRGB>(SysUtil::fullDirPath(outPath) + "allPlanes.pcd",
					   *allPlanes, true);
	    writer.write<pcl::PointXYZRGB>(SysUtil::fullDirPath(outPath) + "nonPlanes.pcd",
					   *intermediateCloud, true);
	    std::cout << "Done." << std::endl;

	    // colour the inliers so we can tell them apart easily
	    for (auto it = allPlanes->begin(); it != allPlanes->end(); it++) {
		it->r = 255;
		it->b = 255;
	    }

	    // After the process is finished, combine the extracted planes and the other
	    // points together again so that they can be displayed
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr fullCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
	    *fullCloud = *allPlanes + *remainingPoints;

	    boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer =
		boost::shared_ptr<pcl::visualization::PCLVisualizer>(new pcl::visualization::PCLVisualizer("Cloud viewer"));
	    std::string cloudName("cloud");
	    pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> rgb(fullCloud);
	    viewer->setBackgroundColor(0,0,0);
	    viewer->addPointCloud(fullCloud, rgb, cloudName.c_str());
	    viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE,
						     2, cloudName.c_str());
	    viewer->initCameraParameters();
    
	    while (!viewer->wasStopped()) {
		viewer->spinOnce(100);
		boost::this_thread::sleep(boost::posix_time::microseconds(100000));
	    }
	}
    } // namespace preprocessing
} // namespace objsearch



int main(int argc, char *argv[]) {
    objsearch::preprocessing::PreprocessRoom rp(argc, argv);
}
