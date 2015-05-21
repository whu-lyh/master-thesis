#include "objectQuery.hpp"

namespace objsearch {
    namespace objectquery {
	ObjectQuery::ObjectQuery(int argc, char *argv[]){
	    ros::init(argc, argv, "object_query");
	    ros::NodeHandle handle;

	    // Retrieve the directory containing the cloud to be processed
	    ROSUtil::getParam(handle, "/object_query/query_features", queryFile_);
	    // Retrieve the directory containing the cloud of features generated by
	    // feature extraction
	    ROSUtil::getParam(handle, "/object_query/target_features", targetFile_);
	    ROSUtil::getParam(handle, "/object_query/output_regions", outputRegions_);

	    // Extract the K parameter specified in the launch file. If it is
	    // still at the default value of -1, read the parameter from the
	    // param file instead.
	    ROSUtil::getParam(handle, "/object_query/K", K_);
	    if (K_ == -1){
		ROSUtil::getParam(handle, "/obj_search/object_query/K", K_);
	    }

	    ROSUtil::getParam(handle, "/obj_search/processed_data_dir", dataPath_);
	    ROSUtil::getParam(handle, "/object_query/output_dir", outDir_);

	    ROSUtil::getParam(handle, "/object_query/x_step_hough", xStepHough_);
	    ROSUtil::getParam(handle, "/object_query/y_step_hough", yStepHough_);
	    ROSUtil::getParam(handle, "/object_query/z_step_hough", zStepHough_);

	    ROSUtil::getParam(handle, "/object_query/n_max_points", nMax_);
	    
	    // If output is not specified, set the output directory to be the processed
	    // data directory specified by the global parameters.
	    if (std::string("NULL").compare(outDir_) == 0) {
		outDir_ = dataPath_;
	    }

	    // Query info is always the same, so initialise everything here.
	    // Stuff about the target will be initialised every loop in the
	    // doSearch function
	    
	    // extract the remaining directories in the path of the file so that
	    // the data can be put into the output directory with the same path
	    // following it.
	    if (queryFile_.compare(0, dataPath_.size(), dataPath_) == 0){
		dataSubDir_ = sysutil::trimPath(std::string(queryFile_, dataPath_.size()), 1);
	    }
	    
	    // The output path for processed clouds is the subdirectory combined
	    // with the top level output directory. If dataSubDir_ is not
	    // initialised, then clouds are simply output to the top level
	    // output directory. The output will be written to a location for
	    // the query cloud, for each of the target clouds used.
	    outPath_ = sysutil::fullDirPath(sysutil::combinePaths(outDir_, dataSubDir_));
	    ROS_INFO("Output path is %s", outPath_.c_str());
	    // the file containing the feature locations of the query file can
	    // be found in the same directory, with the same date-time stamp. We
	    // can use a regex to find the corresponding file, of which there
	    // should be only one. The query file format is something like
	    // originfile<descriptortype_interesttype_date_time.pcd
	    // remove the preceding path and the extension from the query file
	    std::string trimmedName = sysutil::removeExtension(queryFile_, -1);
	    ROS_INFO("Trimmed name is %s", trimmedName.c_str());
	    // we know that the date/time makes up the last 19 characters of the
	    // string, and we want to remove the preceding underscore as well
	    std::string dateTime = std::string(trimmedName.begin() + trimmedName.length() - 19,
					       trimmedName.end());
	    ROS_INFO("Date time is %s", dateTime.c_str());
	    
	    // remainder of the string containing information about the original
	    // name, interest and descriptor types
	    std::string remainder = std::string(trimmedName.begin(), trimmedName.begin() + trimmedName.length() - 20);
	    ROS_INFO("Remainder is %s", remainder.c_str());
	    
	    // we are interested in extracting the interest point type and the original filename
	    int indicator = remainder.find_last_of('_');
	    // interest type is the part of the string after the last _
	    std::string interestType = std::string(remainder, indicator + 1);
	    indicator = remainder.find_first_of('<');
	    // the original file name starts at the beginning of the string and
	    // ends at the first <
	    std::string originalFname = std::string(remainder.begin(), remainder.begin() + indicator);
	    // we assume that query objects have their label name in the
	    // filename, following the string label_
	    std::string labelPrefix("label_");
	    int ind = originalFname.find(labelPrefix);
	    if (ind == std::string::npos) {
		throw sysutil::objsearchexception("Filename did not contain label prefix - could not extract a label");
	    } else {
		queryObjectLabel_ = std::string(originalFname, ind + labelPrefix.size());
	    }
	    ROS_INFO("datetime is %s", dateTime.c_str());
	    ROS_INFO("remainder is %s", remainder.c_str());
	    ROS_INFO("interesttype is %s", interestType.c_str());
	    ROS_INFO("original filename is %s", originalFname.c_str());
	    ROS_INFO("object label is %s", queryObjectLabel_.c_str());
	    
	    // now, find files in the directory which have the same interest
	    // type and date+time. There should be only one, if there are any at
	    // all.
	    std::vector<std::string> matches = sysutil::listFilesWithString(sysutil::trimPath(queryFile_, 1), std::regex(".*points_" + interestType + ".*" + dateTime + ".*"));

	    if (matches.size() == 0){
		ROS_INFO("No matches for descriptor location file of %s", queryFile_.c_str());
		exit(1);
	    }
	    
	    queryPointFile_ = matches[0];
	    pcl::PCDReader reader;
	    pcl::PCLPointCloud2 queryHeader;
	    reader.readHeader(queryFile_, queryHeader);
	    queryType_ = queryHeader.fields[0].name;
	    
	    ROS_INFO("Loading query feature points from %s", queryPointFile_.c_str());

	    std::string match;
	    ROSUtil::getParam(handle, "/object_query/match", match);
	    // Get all paths to the target clouds to check. This depends on
	    // whether the parameter given is a file or directory, and also
	    // whether a match string was given or not.
	    if (sysutil::isDir(targetFile_)) {
		if (match.compare("NULL") == 0) {
		    targetClouds_ = sysutil::listFilesWithString(targetFile_, "nonPlanes.pcd", true);
		} else { // otherwise, match the string and process those files
		    targetClouds_ = sysutil::listFilesWithString(targetFile_, match, true);
		}
	    } else { // is a file, so just process that
		targetClouds_.push_back(targetFile_);
	    }

	    // for simplicity, only look at clouds which have an annotation for
	    // the object that is being queried.
	    std::vector<int> toKeep;
	    for (size_t i = 0; i < targetClouds_.size(); i++) {
		// annotations are assumed to be found in the directory above
		// the target cloud
		std::string annotationDir = sysutil::trimPath(targetClouds_[i], 2);
		std::vector<std::string> annotationFiles = sysutil::listFilesWithString(annotationDir, queryObjectLabel_ + ".pcd");
		// there should be one and only one result.
		if (annotationFiles.size() == 1) {
		    toKeep.push_back(i);
		}
	    }

	    // not very efficient, but only needs to be done once
	    for (size_t i = 0; i < toKeep.size(); i++) {
		targetClouds_[i] = targetClouds_[toKeep[i]];
	    }
	    targetClouds_.resize(toKeep.size());

	    // error if there are no target clouds to look at.
	    if (targetClouds_.size() == 0) {
		ROS_INFO("No target clouds found in the specified location, or they were filtered out.");
		throw sysutil::objsearchexception("Did not extract any valid target clouds.");
	    }

	    // print some information about what files will be processed
	    size_t i;
	    for (i = 0; i < 10 && i < targetClouds_.size(); i++) {
		ROS_INFO("%s", targetClouds_[i].c_str());
	    }
	    if (i >= 10) {
		ROS_INFO("And more...");
	    }

	    std::string dataOutput = sysutil::fullDirPath(outPath_);

	    // Depending on the type of the descriptor in the cloud, we need to
	    // instantiate a different template for the search function
	    if (queryType_.find("shot") != std::string::npos) {
		// the only way to distinguish between colour shot and normal
		// shot is by checking the dimensionality of the descriptor
		int count = queryHeader.fields[0].count;
		if (count == 352) {
		    doSearch<pcl::SHOT352>();
		} else if (count == 1344) {
		    doSearch<pcl::SHOT1344>();
		} else {
		    ROS_ERROR("Unknown descriptor field specifier: %s", queryType_.c_str());
		    throw sysutil::objsearchexception("Unknown descriptor field specifier");;
		}
	    } else if (queryType_.find("pfh") != std::string::npos) {
		if (queryType_.compare("pfh") == 0) {
		    doSearch<pcl::PFHSignature125>();
		} else if (queryType_.compare("fpfh") == 0) {
		    doSearch<pcl::FPFHSignature33>();
		} else if (queryType_.compare("pfhrgb") == 0) {
		    doSearch<pcl::PFHRGBSignature250>();
		} else {
		    ROS_ERROR("Unknown descriptor field specifier: %s", queryType_.c_str());
		    throw sysutil::objsearchexception("Unknown descriptor field specifier");;
		}
	    } else if (queryType_.compare("shape_context") == 0) {
		doSearch<pcl::ShapeContext1980>();
	    } else {
		ROS_ERROR("Unknown descriptor field specifier: %s", queryType_.c_str());
                throw sysutil::objsearchexception("Unknown descriptor field specifier");;
	    }
	}

	bool ObjectQuery::initAndCheckPaths(std::string path) {
	    targetFile_ = path;
	    // Read the headers for the point clouds that were provided as
	    // input, and look at the field names to determine which descriptor
	    // type is stored in the cloud.
	    pcl::PCDReader reader;
	    pcl::PCLPointCloud2 targetHeader;
	    reader.readHeader(targetFile_, targetHeader);
	    std::string targetType = targetHeader.fields[0].name;

	    // The descriptors for both clouds must be the same, otherwise we
	    // cannot compare them.
	    if (queryType_.compare(targetType) != 0){
		ROS_ERROR("Fields of the two descriptor clouds do not match: \n"\
			  "Query: %s, target: %s", queryType_.c_str(), targetType.c_str());
		return false;
	    }

	    std::string trimmedName = sysutil::removeExtension(targetFile_, -1);
	    // we know that the date/time makes up the last 19 characters of the
	    // string, and we want to remove the preceding underscore as well
	    std::string dateTime = std::string(trimmedName.begin() + trimmedName.length() - 19,
					       trimmedName.end());
	    // remainder of the string containing information about the original
	    // name, interest and descriptor types
	    std::string remainder = std::string(trimmedName.begin(), trimmedName.begin() + trimmedName.length() - 20);

	    // we are interested in extracting the interest point type and the original filename
	    int indicator = remainder.find_last_of('_');
	    // interest type is the part of the string after the last _
	    std::string interestType = std::string(remainder, indicator + 1);
	    indicator = remainder.find_first_of('<');
	    // the original file name starts at the beginning of the string and
	    // ends at the first <
	    std::string originalFname = std::string(remainder.begin(), remainder.begin() + indicator);
	    
	    ROS_INFO("datetime is %s", dateTime.c_str());
	    ROS_INFO("remainder is %s", remainder.c_str());
	    ROS_INFO("interesttype is %s", interestType.c_str());
	    ROS_INFO("original filename is %s", originalFname.c_str());
	    
	    // now, find files in the directory which have the same interest
	    // type and date+time. There should be only one, if there are any at
	    // all.
	    std::vector<std::string> matches = sysutil::listFilesWithString(sysutil::trimPath(queryFile_, 1), std::regex(".*points_" + interestType + ".*" + dateTime + ".*"));

	    if (matches.size() == 0){
		ROS_INFO("No matches for descriptor location file of %s", targetFile_.c_str());
		throw sysutil::objsearchexception("No matches for descriptor location file.");
	    }
	    
	    targetPointFile_ = matches[0];

	    ROS_INFO("Loading target feature points from %s", targetPointFile_.c_str());

	    return true;
	}

	/** 
	 * Annotate points in the given cloud, using oriented bounding boxes
	 * computed from the annotated clouds.
	 *
	 * @param dir The top level room directory containing the cloud of
	 * interest (and more specifically the annotated clouds).
	 * @param cloud The cloud containing points to annotate.
	 * @param label the label of the specific annotation that we are
	 * interested in using to label points
	 * @param indices This vector will be populated with the indices of the
	 * points which have been labelled
	 * @param labels Will be populated with the labels of the points
	 */
	void ObjectQuery::annotatePointsOBB(
	    std::string dir, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
	    std::vector<int>& indices, std::vector<std::string>& labels,
	    std::string queryLabel) {
	    // load all the annotated clouds and compute their bounding boxes.
	    // Don't care about rgb values
	    indices.clear();
	    labels.clear();
	    std::vector<pclutil::AnnotatedCloud<pcl::PointXYZ> > annotations
		= pclutil::getProcessedAnnotatedClouds<pcl::PointXYZ>(dir, queryLabel);
	    if (annotations.size() == 0){
		throw sysutil::objsearchexception("No annotation clouds were found in annotatePointsOBB.");
	    }

	    // fill a vector with the bounding boxes of each of the annotated clouds
	    std::vector<pclutil::OrientedBoundingBox> bboxes;
	    for (auto it = annotations.begin(); it != annotations.end(); it++) {
		bboxes.push_back(pclutil::getOrientedBoundingBox(it->cloud, it->label));
	    }

	    // go through each point in the cloud given and check if it lies in
	    // any of the bounding boxes of objects. If it lies in multiple
	    // boxes, the point will be added to the indices and labels arrays
	    // multiple times.
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformed(
		new pcl::PointCloud<pcl::PointXYZRGB>());
	    for (size_t i = 0; i < annotations.size(); i++) {
		ROS_INFO("Checking bbox for %s", annotations[i].label.c_str());
		// transform the points in the cloud into the frame of the box.
		pcl::transformPointCloud(*cloud, *transformed, bboxes[i].transformInverse);

		std::string currentLabel = annotations[i].label;
		for (size_t j = 0; j < transformed->size(); j++) {
		    if (bboxes[i].contains(transformed->points[j], true)){
			indices.push_back(j);
			labels.push_back(currentLabel);
		    }
		}
	    }
	}

	/** 
	 * Annotate points in a cloud loaded from a certain directory based on
	 * the annotations. Points in the given cloud will be compared to the
	 * annotated objects, and labelled with the label of the nearest object,
	 * but only if the point is within a euclidean distance of maxDist of
	 * the object. Uses nearest neighbour search for each point in the query
	 * cloud.
	 * 
	 * @param dir The top level room directory containing the cloud of
	 * interest.
	 * @param cloud The cloud containing points to annotate.
	 * @param indices This vector will be populated with the indices of the
	 * points which have been labelled
	 * @param labels Will be populated with the labels of the points
	 * @param distances Will be populated with the minimum distance of the point to its object
	 * @param maxDist The maximum distance from an object a point can be to
	 * still be considered part of the object
	 */
	void ObjectQuery::annotatePointsCloud(
	    std::string dir, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
	    std::vector<int>& indices, std::vector<std::string>& labels,
	    std::vector<float>& distances, float maxDist) {

	    // extract the annotated clouds from the raw directory along with their labels
	    std::vector<pclutil::AnnotatedCloud<pcl::PointXYZRGB> > annotations
		= pclutil::getProcessedAnnotatedClouds<pcl::PointXYZRGB>(dir);

	    pcl::KdTreeFLANN<pcl::PointXYZRGB> searchTree;
	    // want to find the minimum distance and the corresponding index.

	    pcl::PointXYZRGB point;
	    int minInd = 0; // index of the point closest to the current object
	    float minDist = std::numeric_limits<float>::max(); // minimum distance from the point to an object
	    std::vector<int> nn(1); // index of nearest point on annotated object
	    std::vector<float> pointDistance(1); // distance of point from annotated object

	    // look through all the points in the cloud to be annotated
	    for (size_t i = 0; i < cloud->size(); i++) {

		point = cloud->points[i];

		// reset the min index and distance for the new point
		minInd = 0;
		minDist = std::numeric_limits<float>::max();
		// look through all the annotated object clouds and find the nearest
		// neighbour to the point received.
		for (size_t j = 0; j < annotations.size(); j++) {
		    searchTree.setInputCloud(annotations[j].cloud);
		    searchTree.nearestKSearch(point, 1, nn, pointDistance); // search for 1-nn
		    // update index and minimum distance to the object
		    if (pointDistance[0] < minDist){
			minInd = j;
			minDist = pointDistance[0];
		    }
		}

		// if the point is within the requested distance of the object,
		// push information about the point onto the vectors
		if (minDist < maxDist){
		    indices.push_back(i);
		    labels.push_back(annotations[minInd].label);
		    distances.push_back(minDist);
		    ROS_INFO("Point %d label %s", (int)i, labels.back().c_str());
		} else {
		    ROS_INFO("Point %d not labelled", (int)i);
		}
		
	    }
	}

	/** 
	 * Perform hough voting given the information provided. For each index
	 * in the indices given, which refer to points in the targetPoints
	 * cloud, we add a vote to a cell in 3d space. The votes should indicate
	 * regions where objects of interest lie
	 * 
	 * @param targetPoints The points to use to index into the 3d space
	 * which the grid defines
	 * @param indices The indices of the points at
	 * which we wish to place votes
	 * @param distances The distances to the points
	 */
	pclutil::Grid3D ObjectQuery::houghVoting(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& targetPoints,
				      const std::vector<std::vector<int> >& indices,
				      const std::vector<std::vector<float> >& distances) {
	    // to construct the grid, need to know the bounds of the target
	    // points. Doesn't really matter if we waste some space on extra
	    // cells because the grid is not rotated (hopefully)
	    pcl::PointXYZRGB min;
	    pcl::PointXYZRGB max;
	    pcl::getMinMax3D(*targetPoints, min, max);
	    ROS_INFO("min: %f, %f, %f", min.x, min.y, min.z);
	    ROS_INFO("max: %f, %f, %f", max.x, max.y, max.z);
	    ROS_INFO("Grid dimensions %f, %f, %f", max.x - min.x, max.y - min.y, max.z - min.z);
	    pclutil::Grid3D grid(max.x - min.x, max.y - min.y, max.z - min.z,
				 xStepHough_, yStepHough_, zStepHough_,
				 min.x, min.y, min.z);

	    // Go through all points in the nearest neighbours
	    for (size_t i = 0; i < indices.size(); i++) {
		for (size_t j = 0; j < indices[i].size(); j++) {
		    pcl::PointXYZRGB cur = targetPoints->points[indices[i][j]];
		    grid.at(cur.x, cur.y, cur.z)++;
		}
	    }

	    return grid;
	}

	/** 
	 * Do a nearest neighbour search for the features in the query cloud in
	 * the target cloud. 
	 * 
	 */
	template<typename DescType>
	void ObjectQuery::doSearch() {
	    ROS_INFO("Doing descriptor search.");
	    QueryInfo info = {};
	    
	    pcl::PCDReader reader;

	    // Read the input clouds for the target and query descriptors. We
	    // want to find descriptors in targetFeatures which are close to
	    // those in queryFeatures. Need to use typename here because of
	    // dependent scope - what it is depends on the instantiation of the
	    // template argument. Each point in the *Points clouds corresponds
	    // to the location of the feature at the same index in the *Features
	    // clouds
	    typename pcl::PointCloud<DescType>::Ptr queryFeatures(new pcl::PointCloud<DescType>());
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr queryPoints(new pcl::PointCloud<pcl::PointXYZRGB>());
	    reader.read(queryPointFile_, *queryPoints);
	    reader.read(queryFile_, *queryFeatures);

	    // Create a flannsearch object to use to do the NN search
	    typename pcl::search::FlannSearch<DescType, flann::L2_Simple<float> >
		*search(new pcl::search::FlannSearch<DescType, flann::L2_Simple<float> >());
	    // Flann needs to know the point representation so that it can
	    // convert it to its internal format
	    typename pcl::DefaultPointRepresentation<DescType>::ConstPtr
		descRepr(new pcl::DefaultPointRepresentation<DescType>());
	    search->setPointRepresentation(descRepr);
	    
	    // loop over all clouds in the target cloud vector
	    for (size_t i = 0; i < targetClouds_.size(); i++) {
		ROS_INFO("====================Processing target cloud %d of %d====================\n%s", (int)i, (int)targetClouds_.size(), targetClouds_[i].c_str());
		// Do a check to make sure the feature types match. If not, skip
		// this target cloud
		if (!initAndCheckPaths(targetClouds_[i])) {
		    ROS_INFO("Path check failed, skipping...");
		    continue;
		}

		// load the points and computed features for the target cloud
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr targetPoints(new pcl::PointCloud<pcl::PointXYZRGB>());
		typename pcl::PointCloud<DescType>::Ptr targetFeatures(new pcl::PointCloud<DescType>());

		reader.read(targetClouds_[i], *targetFeatures);
		reader.read(targetPointFile_, *targetPoints);
		search->setInputCloud(targetFeatures);
	    
		std::vector<int> indicesTarget;
		std::vector<std::string> labelsTarget;

		ROS_INFO("Starting search");
		// Loop over all points in the query cloud
		std::vector<std::vector<int> > nearest((int)queryFeatures->size());
		std::vector<std::vector<float> > square_dists((int)queryFeatures->size());
		// Initialise vectors to store the closest K points to the query point.
		ros::Time queryStart = ros::Time::now();
		for (int i = 0; i < queryFeatures->size(); i++) {
		    if (i % 50 == 0 && i > 0) {
			ROS_INFO("Query point %d of %d", i + 1, (int)queryFeatures->size());
		    }
		    // some features may have nan values and will crash if not excluded.
		    if (!pclutil::isValid(queryFeatures->points[i])){
			continue;
		    }
		    // Search for the closest K points to the query point
		    search->nearestKSearch(queryFeatures->points[i], K_, nearest[i], square_dists[i]);
		}
		info.queryTime = (ros::Time::now() - queryStart).toSec();
		ROS_INFO("Finished finding neighbours");

		// do hough voting using the computed neighbours
		ros::Time houghStart = ros::Time::now();
		pclutil::Grid3D grid = houghVoting(targetPoints, nearest, square_dists);
		info.houghTime = (ros::Time::now() - houghStart).toSec();

		
		int total = grid.getValuesTotal();
		info.votesTotal = total;
		int empty = grid.getEmptyTotal();
		int size = grid.size();
		info.pointsTotal = size;
		info.pointsNonZero = size - empty;
		ROS_INFO("Hough grid has size %d, containing %d votes. %d cells have no votes -> %d cells have votes", size, total, empty, size - empty);

		std::vector<int> gridHist = grid.valueHistogram();
		info.pointHistogram = vectorToString(gridHist);
		ROS_INFO("Histogram of grid values: %s", info.pointHistogram.c_str());

		// find the n points in the hough grid with the maximum value -
		// this should indicate the point in the target cloud which most
		// closely matches the query object
		std::vector<std::pair<int, int> > maxPoints = grid.getMaxN(nMax_);
		std::vector<int> maxHistogram(maxPoints[0].second + 1);
		info.votesMaxTotal = 0;
		for (size_t i = 0; i < maxPoints.size(); i++) {
		    maxHistogram[maxPoints[i].second]++;
		    info.votesMaxTotal += maxPoints[i].second;
		}
		info.maxHistogram = vectorToString(maxHistogram);

		ROS_INFO("Max point has %d votes.", maxPoints[0].second);
		ROS_INFO("%d max points have %d votes", (int)maxPoints.size(), info.votesMaxTotal);
		ROS_INFO("Max points histogram: %s", info.maxHistogram.c_str());

		// create a cloud containing only the top n points
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr topCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
		topCloud->resize(maxPoints.size());
		for (size_t i = 0; i < maxPoints.size(); i++) {
		    pcl::PointXYZ p = grid.cellCentreFromIndex(maxPoints[i].first);
		    topCloud->points[i].x = p.x;
		    topCloud->points[i].y = p.y;
		    topCloud->points[i].z = p.z;
		    pclutil::rgb colour = pclutil::getHeatColour(maxPoints[i].second, maxPoints[0].second);
		    topCloud->points[i].r = colour.r * 255;
		    topCloud->points[i].g = colour.g * 255;
		    topCloud->points[i].b = colour.b * 255;
		    if (i == 0) {
			ROS_INFO("Max point r %d g %d b %d", topCloud->points[0].r, topCloud->points[0].g, topCloud->points[0].b);
		    }
		}

		pcl::PointCloud<pcl::PointXYZRGB>::Ptr voteCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
		// convert the grid to a point cloud with rgb values
		// representing the number of votes in each cell
		std::vector<int> cellIndices = grid.toPointCloud(voteCloud);
		
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr oneCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
		for (size_t i = 0; i < voteCloud->size(); i++) {
		    pcl::PointXYZRGB p = voteCloud->points[i];
		    if (grid.at(p.x, p.y, p.z) > 1) {
			oneCloud->push_back(p);
		    }
		}

		std::string out = std::string(sysutil::fullDirPath(outPath_) + "houghVotes_" + queryObjectLabel_ + ".pcd");
		ROS_INFO("Hough done, outputting to %s", out.c_str());
		pcl::PCDWriter writer;
		writer.write<pcl::PointXYZRGB>(out, *voteCloud, true);
		writer.write<pcl::PointXYZRGB>(sysutil::removeExtension(out, false) + "_top.pcd", *topCloud, true);
		writer.write<pcl::PointXYZRGB>(sysutil::removeExtension(out, false) + "_one.pcd", *oneCloud, true);

		// will be filled with all of the points in the hough cloud
		// which were within the bounding box of the annotation cloud
		// with the label of the query object we are using.
		std::vector<int> indices;
		// we know what the label will be, but can't nicely write a
		// function which would allow default vector param
		std::vector<std::string> labels;
		annotatePointsOBB(sysutil::trimPath(targetPointFile_, 2), voteCloud,
				  indices, labels, queryObjectLabel_);
		info.pointsInBox = indices.size();
		info.votesInBox = 0;
		info.pointsMaxInBox = 0;
		info.votesMaxInBox = 0;
		std::vector<int> boxHistogram(maxPoints[0].second + 1);
		std::vector<int> boxMaxHistogram(maxPoints[0].second + 1);
		for (size_t i = 0; i < indices.size(); i++) {
		    info.votesInBox += grid.at(cellIndices[indices[i]]);
		    boxHistogram[grid.at(cellIndices[indices[i]])]++;
		    // check the max points vector to see if this point is in
		    // there as well, and if it is increment the values for the
		    // max box.
		    for (size_t j = 0; j < maxPoints.size(); j++) {
			// if the cell index of one of the maximum points
			// matches the cell index of the point in the OBB that
			// we are looking at, then that max point is also in the
			// OBB.
			if (maxPoints[j].first == cellIndices[indices[i]]) {
			    info.pointsMaxInBox++;
			    info.votesMaxInBox += maxPoints[j].second;
			    boxMaxHistogram[maxPoints[j].second]++;
			}
		    }
		}
		info.boxMaxHistogram = vectorToString(boxMaxHistogram);
		info.boxHistogram = vectorToString(boxHistogram);
		ROS_INFO("Box histogram: %s", info.boxHistogram.c_str());
		ROS_INFO("Box max histogram: %s", info.boxMaxHistogram.c_str());

		
		ROS_INFO("Points inside annotation bbox for hough cloud: %d of %d total grid points >0", info.pointsInBox, (int)voteCloud->size());
		ROS_INFO("Votes inside annotation bbox for hough cloud: %d of %d total grid votes", info.votesInBox, grid.getValuesTotal());
		ROS_INFO("Max points inside annotation bbox for hough cloud: %d of %d total max points", info.pointsMaxInBox, (int)maxPoints.size());
		ROS_INFO("Max Votes inside annotation bbox for hough cloud: %d of %d total max votes", info.votesMaxInBox, info.votesMaxTotal);
	    }
	}

	std::string ObjectQuery::vectorToString(std::vector<int> vec) {
	    std::string str;
	    for (size_t i = 0; i < vec.size(); i++) {
		str += std::to_string(vec[i]) + (i + 1 == vec.size() ? "" : ",");		
	    }
	    return str;
	}

	// void outputRegions(){
	//     // if output regions is set, then we want to extract regions around the points at which features were computed
	//     pcl::PointCloud<pcl::PointXYZRGB>::Ptr targetPoints;
	//     pcl::PointCloud<pcl::PointXYZRGB>::Ptr queryPoints;
	//     if (outputRegions_){
	// 	// Use these to find the xyz position of the features in the clouds
	// 	targetPoints = new pcl::PointCloud<pcl::PointXYZRGB>();
	// 	queryPoints = new pcl::PointCloud<pcl::PointXYZRGB>();
	// 	reader.read(targetPointFile_, *targetPoints);
	// 	reader.read(queryPointFile_, *queryPoints);
	//     }

	    
	//     if (outputRegions_) {
	// 	pcl::PCDWriter writer;

	// 	// create kd trees to use to find points within a given radius of a
	// 	// specific point
	// 	pcl::KdTreeFLANN<pcl::PointXYZRGB> kdtreeQuery;
	// 	pcl::KdTreeFLANN<pcl::PointXYZRGB> kdtreeTarget;
	// 	kdtreeQuery.setInputCloud(queryPoints);
	// 	kdtreeTarget.setInputCloud(targetPoints);
	// 	std::vector<int> nn; // indices of points within the radius
	// 	std::vector<float> pointRadiusSquaredDistance; // distances of those points

	// 	float radius = 1;
	// 	// find points withing a given radius of the query point
	// 	kdtreeQuery.radiusSearch(queryPoints->points[0], radius, nn,
	// 				 pointRadiusSquaredDistance);
	    
	// 	ROS_INFO("%d points within radius %f of query point.", (int)nn.size(), radius);

	// 	// create a cloud to hold those points within the spherical region
	// 	pcl::PointCloud<pcl::PointXYZRGB>::Ptr regionPoints(new pcl::PointCloud<pcl::PointXYZRGB>());
	// 	for (int i = 0; i < nn.size(); i++) {
	// 	    regionPoints->push_back(queryPoints->points[nn[i]]);
	// 	}

	// 	// output the cloud
	// 	std::string filePath = sysutil::trimPath(queryFile_, 1);
	// 	std::string queryRegionOut = outPath_ + "queryRegion.pcd";
	// 	ROS_INFO("Outputting query point region to %s", queryRegionOut.c_str());
	// 	writer.write<pcl::PointXYZRGB>(queryRegionOut, *regionPoints, true);

	    
	// 	ROS_INFO("Query point (%f, %f, %f)", queryPoints->points[0].x,
	// 		 queryPoints->points[0].y, queryPoints->points[0].z);

	// 	// Loop over all the nearest neighbours and create regions for them as well.
	// 	for (int i = 0; i < K_; i++) {
	// 	    ROS_INFO("Index: %d, descriptor distance: %f", indices[i],
	// 		     square_dists[i]);
	// 	    ROS_INFO("Target point (%f, %f, %f)", targetPoints->points[indices[i]].x,
	// 		     queryPoints->points[indices[i]].y,
	// 		     queryPoints->points[indices[i]].z);
	// 	    // clear the spherical region to populate it with points in the
	// 	    // region of the target points
	// 	    regionPoints->clear();
	// 	    kdtreeTarget.radiusSearch(targetPoints->points[indices[i]],
	// 				      radius, nn, pointRadiusSquaredDistance);
	// 	    ROS_INFO("%d points within radius %f of matched target point.",
	// 		     (int)nn.size(), radius);
	// 	    // push all points from the radius search into the new cloud
	// 	    for (int j = 0; j < nn.size(); j++) {
	// 		regionPoints->push_back(targetPoints->points[nn[j]]);
	// 	    }

	// 	    // output the region cloud
	// 	    std::string targetRegionOut = outPath_ + "nn" + std::to_string(i) + ".pcd";
	// 	    ROS_INFO("Outputting target point region to %s", targetRegionOut.c_str());
	// 	    writer.write<pcl::PointXYZRGB>(targetRegionOut, *regionPoints, true);
	//      }
	// }
    
    } // namespace objectquery
} // namespace obj_search

int main(int argc, char *argv[]) {
    objsearch::objectquery::ObjectQuery oq(argc, argv);
}
