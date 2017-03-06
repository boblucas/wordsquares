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
#include <queue>
#include <stack>
#include <queue>

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
				if(*(word+1))
				{
					dawg->children.reserve(dawg->children.size() + 1);
					dawg->children.insert(dawg->children.begin() + dawg->getIndex(*word - 'a'), Dawg());
				}
			}

			//go to next letter
			if(dawg->children.size())
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

	unsigned size()
	{
		unsigned s = 1;
		for(Dawg& child : children)
			s += child.size();
		return s;
	}
};

struct CompactDawg
{
	CompactDawg* children;
	uint32_t mask;
	inline unsigned getIndex(char letter) { return __builtin_popcount(mask & ((1 << letter) - 1)); }
	inline CompactDawg* getChild(char letter) { return &children[getIndex(letter)]; }
	CompactDawg() : children(0), mask(0) {}
	CompactDawg(CompactDawg* children, uint32_t mask) : children(children), mask(mask) {}
};

CompactDawg* dawgToArray(Dawg* in)
{
	unsigned s = in->size();
	CompactDawg* compacted = new CompactDawg[s];
	CompactDawg* out = compacted;
	std::queue<Dawg*> q;
	q.push(in);
	while(q.size())
	{
		Dawg* d = q.front();
		*out = CompactDawg(d->children.size() ? out + q.size() : 0, d->mask);
		q.pop();

		for(Dawg& e : d->children)
			q.push(&e);

		++out;
	}
	return compacted;
}

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

void optimizeToplogy(std::vector<Path>& topology)
{
	//Compress id's
	std::vector<unsigned> mapping;
	for(Path& p : topology) for(unsigned& l : p)
		if(std::find(mapping.begin(), mapping.end(), l) == mapping.end())
			mapping.push_back(l);

	std::sort(mapping.begin(), mapping.end());

	unsigned index = 0;
	for(unsigned& i : mapping)
	{
		for(Path& p : topology)
			for(unsigned& l : p)
				if(l == i)
					l = index;
		++index;
	}
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

	optimizeToplogy(topology);
	for(Path& p : topology)
	{
		for(unsigned& l : p) std::cout << l << ' ';
		std::cout << std::endl;
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


std::vector<std::vector<char>> getCombinedPaths(const std::vector<std::vector<char>>& pathIndicesRaw)
{
	std::vector<std::vector<char>> combined;
	combined.reserve(pathIndicesRaw.size());

	for(const auto& paths : pathIndicesRaw)
	{
		std::vector<char> pathsCombined;
		pathsCombined.reserve(paths.size());
		for(char c : paths)
			pathsCombined.push_back(c);
		
		combined.push_back(pathsCombined);
	}
	return combined;
}

inline uint32_t getMask(const std::vector<char>& indices, const std::vector<CompactDawg*>& dawgs)
{
	uint32_t result = 0b11111111111111111111111111;
	for(const char& i : indices)
		result &= dawgs[i]->mask;
	return result;
}

void exhaustiveIterative(std::vector<CompactDawg*>& dawgs, const std::vector<std::vector<char>>& pathIndicesRaw, const std::vector<Path>& originalPaths, int start)
{
	std::vector<std::vector<char>> paths = getCombinedPaths(pathIndicesRaw);

	unsigned letterCount = paths.size();
	char stack[letterCount] = {};
	uint32_t maskStack[letterCount] = {getMask(paths[0], dawgs)};
	CompactDawg* parents[letterCount][dawgs.size()];

	char i = 0;

	if(!maskStack[0])
		return;

	if(start >= 0)
	{
		stack[i] = start;
		if(((maskStack[i] >> start) & 1) == 0)
			return;

		for(const char& d : paths[i])
		{
			parents[i][d] = dawgs[d];
			dawgs[d] = dawgs[d]->getChild(stack[i]);
		}

		++i;
		stack[i] = 0;
		maskStack[i] = getMask(paths[i], dawgs);
	}

	while(i)
	{
		//Move to the next valid child
		stack[i] += __builtin_ctz(maskStack[i] >> stack[i]);

		//If we are not at the last node yet
		if(i < letterCount-1)
		{
			//Move down
			for(const char& d : paths[i])
			{
				parents[i][d] = dawgs[d];
				dawgs[d] = dawgs[d]->getChild(stack[i]);
			}

			++i;
			stack[i] = 0;
			maskStack[i] = getMask(paths[i], dawgs);
		}
		else
		{
			//Print result and move right
			printResults(originalPaths, stack);
			++stack[i];
		}

		//If there are no children left, and we can move up
		while((maskStack[i] >> stack[i]) == 0 && i)
		{
			//Move up
			--i;
			for(const char& d : paths[i])
				dawgs[d] = parents[i][d];
			//And right
			++stack[i];
		}
	}
}

void multithread(std::vector<Dawg*>& dawgs, const std::vector<std::vector<char>>& pathIndicesRaw, const std::vector<Path>& originalPaths)
{
	std::map<Dawg*, CompactDawg*> duplicates;

	std::vector<CompactDawg*> converted;
	converted.reserve(dawgs.size());
	for(Dawg* d : dawgs)
	{
		if(duplicates.count(d))
			converted.push_back(duplicates[d]);
		else
			converted.push_back(duplicates[d] = dawgToArray(d));
	}


	static int letter = 0;

	auto f = [&]()
	{
		while(letter < 26)
		{
			int next = letter++;
			if(next < 26)
			{
				std::vector<CompactDawg*> copy = converted;
				exhaustiveIterative(copy, pathIndicesRaw, originalPaths, next);
			}
		}
	};

	unsigned cores = 1;// std::thread::hardware_concurrency();
	if(cores == 0) cores = 4;
	std::vector<std::thread> threads;
	for(int i = 0; i < cores; i++)
		threads.push_back(std::thread(f));

	for(auto& t : threads)
		t.join();
}

int main(int argc, char* argv[])
{
	//TODO verify that the files actually exist
	if(argc < 3)
	{
		std::cout << "Two arguments are required, a topology file and a dictionary file" << std::endl;
		return 1;
	}

	std::vector<Path> paths = loadTopologyFile(std::string(argv[1]));
	std::vector<Dawg*> dawgs = loadDictionaryFile(std::string(argv[2]), paths);
	multithread(dawgs, invertTopology(paths), paths);
	return 0;
}