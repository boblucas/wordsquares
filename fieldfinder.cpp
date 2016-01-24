#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <stack>

typedef std::vector<unsigned> Path;

//Directed acyclic word graph. for iterating valid words with a given prefix in log time
struct Dawg
{
	//One child for each of the 26 letters value is 0 when that letter is not a possible prefix
	std::vector<Dawg> children;
	Dawg* parent;
	unsigned mask = 0;

	inline unsigned getIndex(char letter)
	{
		return __builtin_popcount(mask & ((1 << letter) - 1));
	}

	inline Dawg* getChild(char letter)
	{
		return &children[getIndex(letter)];
	}

	//Will add word to this dawg
	//any word length is allowed but this data structure does not support end of word tokens so generally you will want all lengths to be the same
	void addWord(const char* word, std::string original)
	{
		Dawg* dawg = this;
		while(*word)
		{
			//Add the next letter as valid if it does not already exist
			if(((dawg->mask >> (*word - 'a')) & 1) == 0)
			{
				dawg->mask |= 1 << (*word - 'a');
				dawg->children.reserve(dawg->children.size() + 1);
				dawg->children.insert(dawg->children.begin() + dawg->getIndex(*word - 'a'), Dawg());
			}

			//go to next letter
			dawg = dawg->getChild(*word - 'a');
			word++;
		}
	}

	void fillParent()
	{
		for(Dawg& child : children)
		{
			child.parent = this;
			child.fillParent();
		}
	}
};

Path normalizePath(const Path& path)
{
	//Get a sorted list of letter-indices
	Path sorted = path;
	std::sort(sorted.begin(), sorted.end());
	sorted.erase( std::unique( sorted.begin(), sorted.end() ), sorted.end() );

	//Convert letter indices to index into previous sorted list
	Path out;
	for(unsigned p : path)
		out.push_back( std::find(sorted.begin(), sorted.end(), p) - sorted.begin() );

	return out;
}

bool followsForm(const Path& path, const std::string& original)
{
	std::string out;
	for(unsigned i = 0; i < path.size(); i++)
	{
		unsigned j = std::find(path.begin(), path.begin() + i, path[i]) - path.begin();
		if(j != i && original[j] != original[i])
			return false;
	}
	return true;
}

std::string transformString(const Path& path, const std::string original)
{
	std::set<unsigned> uniqueElements(path.begin(), path.end());
	std::string out(uniqueElements.size(), '-');

	for(unsigned i = 0; i < path.size(); i++)
		if(std::find(path.begin(), path.begin() + i, path[i]) == path.begin() + i)
			out[path[i]] = original[i];

	return out;
}


//Creates a Dawg for each occuring length in topology
//Returns a vector of the same length as the topology that contains the correct length Dawg for the path at the same index
std::vector<Dawg*> loadDictionaryFile(std::string filename, std::vector<Path> topology)
{
	//Create an empty Dawg for each length in topology
	std::map<Path, Dawg*> lengths;

	for(const Path& p : topology)
	{
		Path normalized = normalizePath(p);
		if(lengths.count(normalized) == 0)
			lengths.insert({normalized, new Dawg()});
	}

	//Iterate every line in the dictionary file
	std::ifstream file(filename);
	std::string line;
	while (std::getline(file, line))
		for(auto& tuple : lengths)
			if(tuple.first.size() == line.size() && followsForm(tuple.first, line))
				tuple.second->addWord(transformString(tuple.first, line).c_str(), line);


	for(auto& tuple : lengths)
		tuple.second->fillParent();

	//Generate vector of dawgs, one for each Path
	std::vector<Dawg*> dawgs;
	dawgs.resize(topology.size());
	unsigned i = 0;
	for(const Path& p : topology)
		dawgs[i++] = lengths[normalizePath(p)];

	return dawgs;
}

//Essentially loads lines of comma seperated integers into a 2D array
std::vector<Path> loadTopologyFile(std::string filename)
{
	std::vector<Path> topology;

	//Iterate each line
	std::ifstream file(filename);
	std::string line;
	while (std::getline(file, line))
	{
		Path path;

		//Lines starting with '#' are comments
		if(line[0] == '#' || line.size() == 0)
			continue;
		
		//Iterate each number
		std::string number;
		std::stringstream lineStream = std::stringstream(line);
		while(std::getline(lineStream, number, ','))
		{
			//Convert string to integer and add to path
			path.push_back(std::stoi(number));
		}
		
		topology.push_back(path);
	}

	return topology;
}

void printResults(const std::vector<Path>& originalPaths, char* stack)
{
	std::set<std::string> occured;
	std::string result = "";
	for(const Path& path : originalPaths)
	{
		std::string word;
		for(const char& c : path)
			word += stack[c] + 'a';

		if(occured.count(word))
			return;

		occured.insert(word);
		result += ' ';
		result += word;
	}

	static std::mutex printMutex;
	printMutex.lock();
	std::cout << result << std::endl;
	printMutex.unlock();
}

//Turn a 2D array of letter indices into a 2D array of letter to path indices
std::vector<std::vector<char>> invertTopology(const std::vector<Path>& paths)
{
	std::vector<std::vector<char>> inverted;
	for(unsigned i = 0; i < paths.size(); i++)
	{
		for(unsigned l : paths[i])
		{
			if(inverted.size() <= l)
				inverted.resize(l + 1);

			inverted[l].push_back(i);
		}
	}
	return inverted;
}


std::vector<std::vector<Dawg**>> getCombinedPaths(std::vector<Dawg*>& dawgs, const std::vector<std::vector<char>>& pathIndicesRaw)
{
	std::vector<std::vector<Dawg**>> combined;
	combined.reserve(pathIndicesRaw.size());

	for(const auto& paths : pathIndicesRaw)
	{
		std::vector<Dawg**> pathsCombined;
		pathsCombined.reserve(paths.size());
		for(char c : paths)
			pathsCombined.push_back(&dawgs[c]);
		
		combined.push_back(pathsCombined);
	}
	return combined;
}

inline unsigned getMask(const std::vector<Dawg**>& dawgs)
{
	unsigned result = 0b11111111111111111111111111;
	for(const Dawg* const* const d : dawgs)
		result &= (*d)->mask;
	return result;
}

void exhaustiveIterative(std::vector<Dawg*>& dawgs, const std::vector<std::vector<char>>& pathIndicesRaw, const std::vector<Path>& originalPaths, char start)
{
	std::vector<std::vector<Dawg**>> paths = getCombinedPaths(dawgs, pathIndicesRaw);

	unsigned letterCount = paths.size();

	char stack[letterCount] = {};
	unsigned maskStack[letterCount] = {};

	maskStack[1] = getMask(paths[1]);
	stack[0] = start;

	char* s = &stack[1];
	unsigned* m = &maskStack[1];
	auto p = paths.begin() + 1;

	const char* const sEnd = s + letterCount - 2;

	if(!maskStack[1])
		return;

	while(s != stack)
	{
		//Move to the next valid child
		*s += __builtin_ctz(*m >> *s);

		//If we are not at the last node yet
		if(s < sEnd)
		{
			//Move down
			for(Dawg**& d : *p)
				*d = (*d)->getChild(*s);

			*(++s) = 0;
			*(++m) = getMask(*(++p));
		}
		else
		{
			//Print result and move right
			printResults(originalPaths, stack);
			++(*s);
		}

		//If there are no children left, and we can move up
		while((*m >> *s) == 0 && s != stack)
		{
			//Move up
			--s;
			--m;
			--p;
			for(Dawg**& d : *p)
				*d = (*d)->parent;
			//And right
			++(*s);
		}
	}
}

void exhaustiveSubRange(const std::vector<Dawg*>& dawgs, const std::vector<std::vector<char>>& pathIndices, const std::vector<Path>& paths, int start, int end, unsigned __int128 anagram)
{
	unsigned __int128 letterValue = 1;
	letterValue <<= start * 5;
	for(int i = start; i < end; i++)
	{
		std::vector<Dawg*> copy = dawgs;
		if(((anagram >> (i * 5)) & 0b11111) == 0)
			goto SKIP_MULTI_LETTER;

		for(const int& path : pathIndices[0])
		{
			copy[path] = dawgs[path]->getChild(i);
			if(!copy[path])
				goto SKIP_MULTI_LETTER;
		}
		exhaustiveIterative(copy, pathIndices, paths, i);
		SKIP_MULTI_LETTER:;
		letterValue <<= 5;
	}
	//std::cout << start << " done" << std::endl;
}

void exhaustiveMultithreaded(const std::vector<Dawg*>& dawgs, const std::vector<std::vector<char>>& pathIndices, const std::vector<Path>& paths,unsigned __int128 anagram)
{
	std::vector<std::thread> threads;
	//This balance was found by experimentation on a big dutch wordlist
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths, 0, 2, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths, 2, 5, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths, 5, 8, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths, 8, 12, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths,12, 15, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths,15, 18, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths,18, 20, anagram));
	threads.push_back(std::thread(exhaustiveSubRange, dawgs, pathIndices, paths,20, 26, anagram));

	for(std::thread& thread : threads)
		thread.join();
}

int main(int argc, char* argv[])
{
	//TODO verify that the files actually exist
	if(argc < 3)
	{
		std::cout << "Two arguments are required, a topology file and a dictionary file" << std::endl;
		return 1;
	}

	unsigned __int128 anagram = 0;
	if(argc == 4)
	{
		for(char* c = argv[3]; *c; c++)
		{
			unsigned __int128 converted = 1;
			converted <<= ((*c) - 'a') * 5;
			anagram += converted;
		}
	}
	else
	{
		anagram = 0xffffffff;
		anagram <<= 64;
		anagram |= 0xffffffff;
	}

	std::vector<Path> paths = loadTopologyFile(std::string(argv[1]));
	std::vector<Dawg*> dawgs = loadDictionaryFile(std::string(argv[2]), paths);
	exhaustiveMultithreaded(dawgs, invertTopology(paths), paths, anagram);
	return 0;
}
