// compile: g++ -std=c++17 generator.cpp tinyxml.cpp -o gen                               ✔
// run: ./gen sheme.xml

// made by antilopinae

#include "tinyxml2.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace tinyxml2;

class Block;

using DestPair = std::pair<int, int>;                            // {destination_sid, destination_port}
using PortMap = std::unordered_map<int, std::vector<DestPair>>;  // map of ports and their destination

class Graph {
public:
    std::vector<PortMap> blockOutputs;  // vector by SID
    std::vector<std::unique_ptr<Block>> blocks; // vector by SID
};

class Block {
public:
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<int, int> inputs;  // {my port, source block sid}
    std::string name;
    std::string type;
    int id;

    Block(int id, const std::string& name, const std::string& type) : id(id), name(sanitizeName(name)), type(type) {}

    std::string getCInputVar(int port, const Graph& graph) const {
        auto it = inputs.find(port);
        if (it == inputs.end()) {
            throw std::runtime_error("Block '" + name + "' (SID: " + std::to_string(id) + ") has no connection to input port " + std::to_string(port));
        }

        int sourceBlockId = it->second; // getting the source block ID
        if (sourceBlockId >= graph.blocks.size() || !graph.blocks[sourceBlockId]) {
            throw std::runtime_error("Source block with SID '" + std::to_string(sourceBlockId) + "' not found.");
        }
        return "nwocg." + graph.blocks[sourceBlockId]->name;
    }

private:
    std::string sanitizeName(const std::string& name) {
        std::string sanitized = name;
        std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
        return sanitized;
    }
};

class XMLParser {
public:
    Graph parse(const std::string& filename) {
        Graph graph;
        XMLDocument doc;

        if (doc.LoadFile(filename.c_str()) != XML_SUCCESS) {
            throw std::runtime_error("Can not open XML file: " + filename);
        }

        XMLElement* system = doc.FirstChildElement("System");

        if (!system) throw std::runtime_error("Invalid Format: Missing <System> tag");

        int maxSid = -1;

        for (XMLElement* blk = system->FirstChildElement("Block"); blk; blk = blk->NextSiblingElement("Block")) {
            if (const char* sidStr = blk->Attribute("SID"))
                maxSid = std::max(maxSid, std::stoi(sidStr));
        }

        if (maxSid == -1)
            return graph;

        graph.blocks.resize(maxSid + 1);
        graph.blockOutputs.resize(maxSid + 1);

        for (XMLElement* blk = system->FirstChildElement("Block"); blk; blk = blk->NextSiblingElement("Block")) {
            const char* sidStr = blk->Attribute("SID");
            const char* type = blk->Attribute("BlockType");
            const char* name = blk->Attribute("Name");

            if (!sidStr || !type || !name)
                continue;

            int sid = std::stoi(sidStr);

            graph.blocks[sid] = std::make_unique<Block>(sid, name, type);

            for (XMLElement* p = blk->FirstChildElement("P"); p; p = p->NextSiblingElement("P")) {
                if (const char* pName = p->Attribute("Name"); pName && p->GetText())
                    graph.blocks[sid]->params[pName] = p->GetText();
            }

            if (XMLElement* portTag = blk->FirstChildElement("Port")) {
                if (const char* portName = findPValue(portTag, "Name"))
                    graph.blocks[sid]->params["PortName"] = portName;
            }
        }

        for (XMLElement* line = system->FirstChildElement("Line"); line; line = line->NextSiblingElement("Line")) {
            const char* srcStr = findPValue(line, "Src");

            if (!srcStr)
                continue;

            if (XMLElement* branch = line->FirstChildElement("Branch")) {
                for (XMLElement* br = branch; br; br = br->NextSiblingElement("Branch")) {
                    if (const char* dstStr = findPValue(br, "Dst"))
                        createConnection(srcStr, dstStr, graph);
                }
            } else { // simple line
                if (const char* dstStr = findPValue(line, "Dst"))
                    createConnection(srcStr, dstStr, graph);
            }
        }

        return graph;
    }

private:
    const char* findPValue(XMLElement* parent, const char* name) {
        if (!parent)
            return nullptr;

        for (XMLElement* p = parent->FirstChildElement("P"); p; p = p->NextSiblingElement("P")) {
            if (const char* pName = p->Attribute("Name"); pName && strcmp(pName, name) == 0)
                return p->GetText();
        }

        return nullptr;
    }

    void parseEndpoint(const std::string& text, int& blockId, int& port) {
        size_t hash = text.find('#');
        size_t colon = text.find(':');

        if (hash == std::string::npos || colon == std::string::npos || hash > colon) {
            throw std::runtime_error("Invalid Format: Src/Dst: " + text);
        }

        blockId = std::stoi(text.substr(0, hash));
        port = std::stoi(text.substr(colon + 1));
    }

    void createConnection(const char* srcStr, const char* dstStr, Graph& graph) {
        int srcBlockId, dstBlockId, srcPort, dstPort;

        parseEndpoint(srcStr, srcBlockId, srcPort);
        parseEndpoint(dstStr, dstBlockId, dstPort);

        if (srcBlockId < graph.blocks.size() && dstBlockId < graph.blocks.size() && graph.blocks[srcBlockId] && graph.blocks[dstBlockId]) {
            graph.blockOutputs[srcBlockId][srcPort].push_back({dstBlockId, dstPort});
            graph.blocks[dstBlockId]->inputs[dstPort] = srcBlockId;
        }
    }
};

class CodeGenerator {
private:
    const Graph& graph;
    std::vector<std::string> bytecode;

public:
    CodeGenerator(const Graph& g) : graph(g) {}

    void generate(const std::string& outFilename) {
        std::vector<int> sortedBlockIds = topologicalSort();
        std::vector<int> delayBlocks;

        bytecode.reserve(graph.blocks.size() * 6);

        for (const auto& id : sortedBlockIds)
            if (graph.blocks[id]->type == "UnitDelay")
                delayBlocks.push_back(id);

        genHeader();
        genStruct(sortedBlockIds);
        genInit(delayBlocks);
        genStep(sortedBlockIds, delayBlocks);
        genExtPorts();

        writeToFile(outFilename);
    }

private:
    std::vector<int> topologicalSort() {
        // Kahns algorithm
        std::vector<int> sortedOrder; // the result - a sorted list of block IDs
        std::unordered_map<int, int> inDegree; // a map for storing the "incoming degree" of each vertex
                                               // key: SID of the block, value: how many blocks depend on it
        std::queue<int> q; // queue for blocks that do not have raw dependencies

        // calculation of initial incoming degrees
        for (const auto& block : graph.blocks) {
            if (!block) // skip the empty slots in the vector
                continue;
            int id = block->id;

            inDegree[id] = (block->type == "UnitDelay") ? 0 : block->inputs.size();

            if (inDegree[id] == 0)
                q.push(id);
        }

        while (!q.empty()) {
            int id = q.front(); q.pop(); // 1. take the finished block from the queue

            sortedOrder.push_back(id); // 2. add it to the result

            if (id < graph.blockOutputs.size()) { // 3. process all the blocks that depend on it
                const auto& port_map = graph.blockOutputs[id];

                for (const auto& [src_port, destinations] : port_map) {
                    for (const auto& destPair : destinations) {
                        int dstId = destPair.first;

                        // 4. reduce the dependency counter of the neighbor
                        if ((--inDegree[dstId]) == 0)
                            q.push(dstId);
                    }
                }
            }
        }

        // checking for cycles
        size_t totalBlocksInLogic = 0;

        for (const auto& block : graph.blocks)
            if (block && block->type != "Outport")
                totalBlocksInLogic++;

        if (sortedOrder.size() < totalBlocksInLogic) {
            throw std::runtime_error("The graph contains a cycle. Topological sorting is not possible.");
        }

        return sortedOrder;
    }

    void genHeader() {
        bytecode.emplace_back("#include \"nwocg_run.h\"");
        bytecode.emplace_back("");
        bytecode.emplace_back("#include <math.h>");
        bytecode.emplace_back("");
    }

    void genStruct(const std::vector<int>& sortedBlockIds) {
        bytecode.emplace_back("static struct");
        bytecode.emplace_back("{");

        for (const auto& id : sortedBlockIds) {
            const auto& block = graph.blocks[id];
            if (block->type != "Outport") {
                bytecode.emplace_back("    double " + block->name + ";");
            }
        }

        bytecode.emplace_back("} nwocg;");
        bytecode.emplace_back("");
    }

    void genInit(const std::vector<int>& delayBlocks) {
        bytecode.emplace_back("void nwocg_generated_init()");
        bytecode.emplace_back("{");

        for (const auto& id : delayBlocks) {
            bytecode.emplace_back("    nwocg." + graph.blocks[id]->name + " = 0.0;");
        }

        bytecode.emplace_back("}");
        bytecode.emplace_back("");
    }

    void genStep(const std::vector<int>& sortedBlockIds, const std::vector<int>& delayBlocks) {
        bytecode.emplace_back("void nwocg_generated_step()");
        bytecode.emplace_back("{");

        // 1. computing part
        for (const auto& id : sortedBlockIds) {
            const auto& block = graph.blocks[id];

            if (block->type == "Sum") {
                std::ostringstream exprStream;
                std::string inputsStr = block->params.count("Inputs") ? block->params.at("Inputs") : "++";

                exprStream << block->getCInputVar(1, graph);

                for (size_t i = 1; i < inputsStr.length(); ++i) {
                    exprStream << " " << inputsStr[i] << " " << block->getCInputVar(i + 1, graph);
                }

                bytecode.emplace_back("    nwocg." + block->name + " = " + exprStream.str() + ";");
            } else if (block->type == "Gain") {
                std::string gainVal = block->params.count("Gain") ? block->params.at("Gain") : "1.0";
                bytecode.emplace_back("    nwocg." + block->name + " = " + block->getCInputVar(1, graph) + " * " + gainVal + ";");
            }
        }

        // 2. updating part
        bytecode.emplace_back("");
        bytecode.emplace_back("    // Update delay blocks state");

        for (const auto& id : delayBlocks) {
            const auto& block = graph.blocks[id];
            bytecode.emplace_back("    nwocg." + block->name + " = " + block->getCInputVar(1, graph) + ";");
        }

        bytecode.emplace_back("}");
        bytecode.emplace_back("");
    }

    void genExtPorts() {
        std::vector<std::string> outportLines, inportLines;

        bytecode.emplace_back("static const nwocg_ExtPort ext_ports[] = {");

        for (const auto& block : graph.blocks) {
            if (!block)
                continue;

            if (block->type == "Inport") {
                std::string portName = block->params.count("PortName") ? block->params.at("PortName") : block->name;
                inportLines.push_back("    { \"" + portName + "\", &nwocg." + block->name + ", 1 },");
            } else if (block->type == "Outport") {
                auto inIt = block->inputs.find(1);

                if (inIt == block->inputs.end()) {
                    throw std::runtime_error("Outport '" + block->name + "' has no input connection.");
                }

                int srcBlockId = inIt->second;

                if (srcBlockId >= graph.blocks.size() || !graph.blocks[srcBlockId]) {
                    throw std::runtime_error("Source block for Outport '" + block->name + "' not found.");
                }

                std::string sourceVar = graph.blocks[srcBlockId]->name;

                outportLines.push_back("    { \"" + block->name + "\", &nwocg." + sourceVar + ", 0 },");
            }
        }

        for (const auto& line : outportLines)
            bytecode.push_back(line);

        for (const auto& line : inportLines)
            bytecode.push_back(line);

        bytecode.emplace_back("    { 0, 0, 0 }");
        bytecode.emplace_back("};");
        bytecode.emplace_back("");

        bytecode.emplace_back("const nwocg_ExtPort* const nwocg_generated_ext_ports = ext_ports;");
        bytecode.emplace_back("const size_t nwocg_generated_ext_ports_size = sizeof(ext_ports);");
    }

    void writeToFile(const std::string& fname) {
        std::ofstream out(fname);

        for (const auto& instr : bytecode)
            out << instr << '\n';

        out.close();

        std::cout << "Generated file: " << fname << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Using: " << argv[0] << " <input.xml> [output.c]" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = (argc > 2) ? argv[2] : "nwocg_generated.c";

    try {
        XMLParser parser;
        Graph graph = parser.parse(inputFile);
        CodeGenerator gen(graph);
        gen.generate(outputFile);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
