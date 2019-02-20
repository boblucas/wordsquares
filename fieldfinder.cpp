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

#include <sys/time.h>

constexpr bool AllowDuplicateWords = true;

typedef std::vector<unsigned> Path;

//Directed acyclic word graph. for iterating valid words with a given prefix in log time
struct Dawg
{
	//One child for each of the 26 letters value is 0 when that letter is not a possible prefix
	std::vector<Dawg> children;
	unsigned mask = 0;

	inline unsigned getIndex(unsigned char letter)
	{
		return __builtin_popcount(mask & ((1 << letter) - 1));
	}

	inline Dawg* getChild(unsigned char letter)
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
					dawg->children.insert(dawg->children.begin() + dawg->getIndex(*word - 'a'), Dawg());
			}

			//go to next letter
			if(dawg->children.size())
				dawg = dawg->getChild(*word - 'a');
			word++;
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
	uint32_t children;
	uint32_t mask;
	inline unsigned getIndex(unsigned char letter) { return __builtin_popcount(mask & ((1 << letter) - 1)); }
	inline CompactDawg* getChild(unsigned char letter) { return (this + children + getIndex(letter)); }
	CompactDawg() : children(0), mask(0) {}
	CompactDawg(uint32_t children, uint32_t mask) : children(children), mask(mask) {}

	uint32_t depth()
	{
		return 1 + (children ? (this + children)->depth() : 0);
	}

	void normalized(std::vector<uint32_t>& out)
	{
		out.push_back((mask << 6) | depth());
		if(children)
			for(unsigned i = 0; i < __builtin_popcount(mask); ++i)
				(this+children)[i].normalized(out);
	}

	void listChildren(std::set<CompactDawg*>& out)
	{
		for(unsigned i = 0; i < __builtin_popcount(mask) && children; ++i)
			out.insert(this + children + i);
	}
};


void compress(std::vector<CompactDawg>& in, std::vector<CompactDawg*>& roots)
{
	std::cout << "Compressing..." << std::flush;
	//Share as much pointers as possible
	std::map<std::vector<uint32_t>, CompactDawg*> nodes;
	std::set<CompactDawg*> removed;

	for(auto i = in.rbegin(); i != in.rend(); ++i)
	{
		if(removed.count(&(*i))) continue;

		std::vector<uint32_t> x;
		i->normalized(x);

		if(nodes.count(x) && 
			( &(*i) + i->children) != (nodes[x] + nodes[x]->children))
		{
			i->listChildren(removed);
			i->children = (uint32_t)((nodes[x] + nodes[x]->children) - &(*i));
		}
		else if(x.size() < 100)
			nodes[x] = &(*i);
	}


	//And re-layout in memory
	std::map<unsigned, unsigned> locations;

	unsigned j = 0;
	for(unsigned i = 0; i < in.size(); ++i)
	{
		if(!removed.count(in.data() + i))
		{
			locations[i] = j;
			in[j] = in[i];
			in[j].children += i;
			++j;
		}
	}
	for(unsigned i = 0; i < roots.size(); ++i)
		roots[i] = in.data() + locations[roots[i] - in.data()];

	in.resize(j);
	for(unsigned i = 0; i < in.size(); ++i)
		in[i].children = locations[in[i].children] - i;

	std::cout << " total nodes: " << in.size() + removed.size() << " after compression: " << in.size() << std::endl;
}

std::vector<CompactDawg> dawgToArray(Dawg* in)
{
	unsigned s = in->size();
	std::vector<CompactDawg> compacted;
	compacted.resize(s);
	auto out = compacted.begin();
	std::queue<Dawg*> q;
	q.push(in);
	while(q.size())
	{
		Dawg* d = q.front();
		*out = CompactDawg(d->children.size() ? q.size() : 0, d->mask);
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

std::map<std::string, std::map<Path, Dawg*>> dictionaryCache;

Dawg* loadDictionaryFile(std::string filename, const Path& path)
{
	Path normalized = normalizePath(path);

	if(dictionaryCache[filename].count(normalized))
		return dictionaryCache[filename][normalized];

	Dawg* dawg = new Dawg();

	std::ifstream file(filename);
	std::string line;
	while (std::getline(file, line))
		if(normalized.size() == line.size() && followsForm(normalized, line))
			dawg->addWord(transformString(normalized, line).c_str(), line);

	if(!dictionaryCache.count(filename) || !dictionaryCache[filename].count(normalized))
		dictionaryCache[filename][normalized] = dawg;

	return dawg;
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

struct Topology
{
	std::vector<Path > paths;
	std::vector<Dawg*> dawgs;
};

//Essentially loads lines of comma seperated integers into a 2D array
Topology loadTopologyFile(std::string filename)
{
	Topology topology;

	//Iterate each line
	std::ifstream file(filename);
	std::string line;
	while (std::getline(file, line))
	{
		Path path;

		//Lines starting with '#' are comments
		if(line[0] == '#' || line.size() == 0)
			continue;
	
		//Get filename of dictionary
		std::string numbers;
		std::string dictionary_filename;
		std::stringstream lineStream = std::stringstream(line);
		std::getline(lineStream, numbers, ':');
		std::getline(lineStream, dictionary_filename, ':');

		//Iterate each number
		std::string number;
		lineStream = std::stringstream(numbers);
		while(std::getline(lineStream, number, ','))
		{
			//Convert string to integer and add to path
			path.push_back(std::stoi(number));
		}
		
		topology.paths.push_back(path);
		topology.dawgs.push_back(loadDictionaryFile(dictionary_filename, path));
	}

	optimizeToplogy(topology.paths);
	std::cout << "optimized topology:" << std::endl;
	for(Path& p : topology.paths)
	{
		for(unsigned& l : p) std::cout << l << ' ';
		std::cout << std::endl;
	}

	return topology;
}

void printResults(const std::vector<Path>& originalPaths, unsigned char* stack)
{
	std::set<std::string> occured;
	std::string result = "";
	for(const Path& path : originalPaths)
	{
		std::string word;
		for(const unsigned char& c : path)
			word += stack[(unsigned)c] + 'a';

		if(!AllowDuplicateWords && occured.count(word))
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
std::vector<std::vector<unsigned char>> invertTopology(const std::vector<Path>& paths)
{
	std::vector<std::vector<unsigned char>> inverted;
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


std::vector<std::vector<unsigned char>> getCombinedPaths(const std::vector<std::vector<unsigned char>>& pathIndicesRaw)
{
	std::vector<std::vector<unsigned char>> combined;
	combined.reserve(pathIndicesRaw.size());

	for(const auto& paths : pathIndicesRaw)
	{
		std::vector<unsigned char> pathsCombined;
		pathsCombined.reserve(paths.size());
		for(unsigned char c : paths)
			pathsCombined.push_back(c);
		
		combined.push_back(pathsCombined);
	}
	return combined;
}

inline uint32_t getMask(const std::vector<unsigned char>& indices, const std::vector<CompactDawg*>& dawgs)
{
	uint32_t result = 0b11111111111111111111111111;
	for(const unsigned char& i : indices)
		result &= dawgs[i]->mask;
	return result;
}

void exhaustiveIterative(std::vector<CompactDawg*>& dawgs, const std::vector<std::vector<unsigned char>>& pathIndicesRaw, const std::vector<Path>& originalPaths, int start)
{
	std::vector<std::vector<unsigned char>> paths = getCombinedPaths(pathIndicesRaw);

	unsigned letterCount = paths.size();
	unsigned char stack[letterCount] = {};
	uint32_t maskStack[letterCount] = {getMask(paths[0], dawgs)};
	CompactDawg* parents[letterCount][dawgs.size()];

	unsigned char i = 0;

	if(!maskStack[0])
		return;

	if(start >= 0)
	{
		stack[i] = start;
		if(((maskStack[i] >> start) & 1) == 0)
			return;

		for(const unsigned char& d : paths[i])
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
			for(const unsigned char& d : paths[i])
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
			for(const unsigned char& d : paths[i])
				dawgs[d] = parents[i][d];
			//And right
			++stack[i];
		}
	}
}

void multithread(std::vector<Dawg*>& dawgs, const std::vector<std::vector<unsigned char>>& pathIndicesRaw, const std::vector<Path>& originalPaths)
{
	std::map<Dawg*, std::vector<CompactDawg>> duplicates;
	std::map<Dawg*, CompactDawg*> duplicates_single;

	for(Dawg* d : dawgs)
		if(!duplicates.count(d))
			duplicates[d] = dawgToArray(d);

	unsigned s = 0;
	for(auto& x: duplicates)
		s += x.second.size();
	std::vector<CompactDawg> all;
	all.reserve(s);

	for(auto& x: duplicates)
	{
		duplicates_single[x.first] = all.data() + all.size();
		for(auto& y: x.second)
			all.push_back(y);
	}

	std::vector<CompactDawg*> converted;
	converted.reserve(dawgs.size());
	for(Dawg* d : dawgs)
		converted.push_back(duplicates_single[d]);

	compress(all, converted);


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

	unsigned cores = 0;// std::thread::hardware_concurrency();
	if(cores == 0) cores = 4;
	std::vector<std::thread> threads;
	for(int i = 0; i < cores; i++)
		threads.push_back(std::thread(f));

	auto start = clock();
	for(auto& t : threads)
		t.join();
	std::cout << "took: " << (double)(clock() - start)  / CLOCKS_PER_SEC << 's' << std::endl;
}

int main(int argc, char* argv[])
{
	//TODO verify that the files actually exist
	if(argc < 2)
	{
		std::cout << "At least a single argument is expected, the topology file." << std::endl;
		return 1;
	}

	for(int c = 1; c < argc; ++c)
	{
		Topology topology = loadTopologyFile(std::string(argv[c]));
		multithread(topology.dawgs, invertTopology(topology.paths), topology.paths);
	}
	return 0;
}