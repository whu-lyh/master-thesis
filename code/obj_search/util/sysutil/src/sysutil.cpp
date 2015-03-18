/**
 * @file   sysutil.cpp
 * @author Michal Staniaszek <michalst@kth.se>
 * @date   Tue Mar 10 11:10:10 2015
 * 
 * @brief  
 * 
 * 
 */
#include "sysutil.hpp"
#include <iostream>

/**
 * @namespace SysUtil Namespace for system utilities like directory interaction
 */
namespace SysUtil {
    
    /** 
     * Check if the path given is of a certain type.
     *
     * See [the GNU documentation](
     * http://www.gnu.org/software/libc/manual/html_node/Testing-File-Type.html#Testing-File-Type)
     * for possible types.
     * 
     * @param path The path to check.
     * @param mode Check if the path is of this mode type.
     * 
     * @return True if \p path is of the given \p mode.
     */
    bool isType(const std::string& path, mode_t mode) {
	struct stat s;
	if (stat(path.c_str(), &s) == 0) {
	    if (s.st_mode & mode) {
		return true;
	    } else {
		return false;
	    }
	} else {
	    return false;
	}
    }

    /** 
     * Check if the given path is a directory.
     * 
     * @param path Path to check.
     * 
     * @return True if \p path is a directory.
     */
    bool isDir(const std::string& path) {
	return isType(path, S_IFDIR);
    }

    /** 
     * Check if the given path is a file.
     * 
     * @param path Path to check.
     * 
     * @return True if \p path is a file.
     */
    bool isFile(const std::string& path) {
	return isType(path, S_IFREG);
    }


    /**
     * Removes the trailing slash from a directory path if there is one.
     * 
     * For example, `/home/user/data/stuff/` would be converted to `/home/user/data/stuff`
     * 
     * @param path Path string to clean up.
     * 
     * @return \p path with the last character  a `/`, 
     */
    std::string cleanDirPath(std::string path) {
	std::string dir = path;
	if (path[path.size() - 1] == '/'){
	    dir = path.substr(0, dir.size() - 1);
	}
	return dir;
    }

    /**
     * Ensures that the given path ends with a `/` if it does not already. If
     * the path provided is not a directory, the original string is returned.
     * Assumes that the path given is actually a directory and not a file. If it
     * is a file you will end up with a trailing slash on that.
     *
     * For example, `/home/user/data/stuff` would be converted to `/home/user/data/stuff/`
     * 
     * @param path The path string to extend.
     * 
     * @return \p path with `/` as the last character.
     */
    std::string fullDirPath(std::string path) {
	std::string dir = path;
	if (path[path.size() - 1] != '/'){
	    dir = dir + "/";
	}
	return dir;
    }

    /** 
     * Trim the given path back by \p nToTrim elements. A path element is either
     * a file or a directory.
     *
     * The \p path `/home/user/data/file.txt` has four elements. Three are
     * directories and one is a filename. The elements are trimmed from the end
     * of the path by default. Calling the function on this path with \p
     * nToTrim=1 returns `/home/user/data`. The same with \p fromFront=true
     * gives `user/data/file.txt`. Calling with \p nToTrim=-1 gives `file.txt`,
     * and with \p fromFront=true as well, gives `/home`.
     * 
     * @param path The path to trim
     * @param nToTrim The number of elements to
     * trim from the path. If the argument given is negative, the behaviour
     * changes so that instead of trimming the elements from the starting point,
     * it trims them from the end point, leaving the number of elements
     * specified by this parameter.
     * @param fromFront When true, trim the elements from the front of the path
     * instead of the back.
     * 
     * @return A path with the given number of elements trimmed. If \p nToTrim
     * exceeds the total number of elements, then you will be left with either a
     * filename if \p fromFront is true, or an empty string otherwise. Trimming
     * `/home/user/data/file.txt` by 2 elements from the back would give you
     * `/home/user`. Notice that there is no trailing slash. Going from the
     * front the return would be `data/file.txt`.
     */
    std::string trimPath(std::string inPath, int nToTrim, bool fromFront){
	if (nToTrim == 0){ return inPath; }

	std::string path = inPath;
	// Whether we want to modify the behaviour to leave nToTrim elements, or
	// to take them away
	bool reverse = false;
	if (nToTrim < 0){
	    reverse = true;
	    nToTrim = -nToTrim;
	}
	
	if (fromFront && !reverse && path[0] == '/') {
	    // if starting from the root, skip the first character of the path
	    path = std::string(path, 1);
	}

	int nextDir;
	if (fromFront) {
	    nextDir = -1; // start it off at -1 so the loop is clean
	    // keep going until we have gathered the number of elements to trim,
	    // and while the first directory delimiter exists. The nextDir
	    // variable is updated at each loop, and we only examine the part
	    // of the string which begins at nextDir + 1. When the loop ends, the 
	    while (nToTrim-- > 0
		   && (nextDir = path.find_first_of("/", nextDir + 1)) != (int)std::string::npos);
	    // Depending on whether the reverse flag is true, we either return
	    // the front part of the string, or the end part. If reversed,
	    // return the front part of the string, otherwise return the back.
	    path = reverse ? std::string(path, 0, nextDir) : std::string(path, nextDir + 1);
	} else {
	    nextDir = 0;
	    // Keep going until the number of elements requested to trim have
	    // been gathered, and the last delimiter in the string exists. We
	    // are going from the back of the string, so look for the last
	    // directory delimiter and then update the nextDir variable to point
	    // to that. In the next loop, only look at the part of the string
	    // that preceeds that delimiter to find the next one.
	    while (nToTrim-- > 0
		   && (nextDir = path.find_last_of("/", nextDir - 1)) != (int)std::string::npos
		   && nextDir != 0); // edge case where there is a / in first position
	    // Depending on whether the reverse flag is true, we either return
	    // the front part of the string, or the end part. If reversed,
	    // return the back part of the string, otherwise return the front.
	    path = reverse ? std::string(path, nextDir + 1) : std::string(path, 0, nextDir);
	}

	// If there are still elements left over when the loop has completed
	// (std::string::npos was reached), then either return the full string,
	// or the empty string, depending on whether the reverse flag is true
	if (nToTrim > 0) {
	    return reverse ? inPath : "";
	}

	return path;
    }
    
    /**
     * Removes the base of a path, leaving either the directory or the file
     * name.
     *
     * For example, if \p path is `/home/user/data/file.txt`, return is
     * `file.txt`. If \p path is `/home/user/data/` then the return is `data`.
     * 
     * @param path Path to trim
     * 
     * @return A filename or directory name corresponding to the last name in the path.
     */
    std::string removePathBase(std::string path) {
	std::string cleanPath = cleanDirPath(path);
	int lastDir = cleanPath.find_last_of("/");
	return cleanPath.substr(lastDir + 1);
    }

    /** 
     * Make directories lying on a given path. Works like `mkdir -p`.
     *
     * For example, if `/tmp` exists, and this function is called on
     * `/tmp/some/dir/here`, all the directories in the path which did not yet
     * exist will be created.
     * 
     * @param path The path of the directory to create.
     * @return True if all directories which did not exist were successfully
     * created, or if the directory path already existed, false if one or more
     * of the directories that were supposed to be created were not.
     */
    bool makeDirs(std::string path){
	if (isDir(path)) {
	    return true;
	}
	
	int trimNum = 0; // start off by trimming nothing
	std::string tPath; // store a trimmed path here
	std::vector<std::string> toCreate; // store the paths we need to create here
	// Trim the path repeatedly from the back (e.g. /usr/home/michal/item ->
	// /usr/home/michal -> /usr/home -> /usr) until a path is found which is
	// already a directory. Push the paths which aren't directories onto the
	// vector and create them in reverse order so that the parent directory
	// always exists when creating a child.
	while (!isDir(tPath = trimPath(path, trimNum++))){
	    toCreate.push_back(tPath);
	}

	// If no directories were added to the vector then we aren't going to create any
	if (toCreate.empty()) {
	    return false;
	}

	bool ret = true;
	// Go treat the vector as a stack, and create directories from highest
	// to lowest in the hierarchy to ensure that the parent exists before
	// creating the child. If an error occurs, change the return value to
	// indicate that some error occurred.
	while (!toCreate.empty()) {
	    int r = mkdir(toCreate.back().c_str(), S_IRWXU | S_IRWXG | S_IROTH);
	    if (r == -1) {
		std::cout << "Did not mkdir " << path << ": ";
		perror("Reason");
		ret = false;
	    }
	    toCreate.pop_back();
	}
	return ret;
    }


    /** 
     * Combines two paths into a single path by concatenating them, ensuring
     * that the path is a valid one by removing `~/`, `../` and `./` from the
     * front of the second string.
     * 
     * @param a 
     * @param b 
     * 
     * @return 
     */
    std::string combinePaths(std::string a, std::string b){
	if (isFile(a)) { // need to remove the filename
	    int lastDir = a.find_last_of('/');
	    a = std::string(a, 0, lastDir);
	}

	bool trimmed = false;
	while (!trimmed) {
	    if (b[0] == '/'){
		b = std::string(b, 1);
	    } else if (b.compare(0, 2, "~/") == 0
		       || b.compare(0, 2, "./") == 0) {
		b = std::string(b, 2);
	    } else if (b.compare(0, 3, "../") == 0) {
		b = std::string(b, 3);
	    } else {
		b = cleanDirPath(b);
		trimmed = true;
	    }
	}

	return fullDirPath(a) + b;
    }

} // namespae SysUtil