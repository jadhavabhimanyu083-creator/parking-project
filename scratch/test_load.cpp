#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <set>
#include "Json.hpp"
#include "Models.hpp"

std::vector<std::string> splitJsonObjects(const std::string& arrayJson) {
    std::vector<std::string> objs;
    bool inQuote = false;
    bool escape = false;
    int braceDepth = 0;
    size_t objStart = 0;

    for (size_t i = 0; i < arrayJson.length(); ++i) {
        char c = arrayJson[i];
        if (inQuote) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inQuote = false;
            }
        } else {
            if (c == '"') {
                inQuote = true;
            } else if (c == '{') {
                if (braceDepth == 0) {
                    objStart = i;
                }
                braceDepth++;
            } else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    objs.push_back(arrayJson.substr(objStart, i - objStart + 1));
                }
            }
        }
    }
    return objs;
}

int main() {
    std::ifstream pf("data/plots.json");
    if (!pf.is_open()) {
        std::cerr << "Failed to open data/plots.json\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << pf.rdbuf();
    pf.close();
    std::string content = buffer.str();

    std::vector<std::string> plotObjs = splitJsonObjects(content);
    std::cout << "Found " << plotObjs.size() << " plots in JSON\n";

    std::set<std::string> dummyPlots = {"plot_phoenix_marketcity", "plot_sriook_park", "plot_zoob_residences", "plot_tower_43"};

    for (size_t i = 0; i < plotObjs.size(); ++i) {
        const std::string& po = plotObjs[i];
        std::string id = Json::getStringField(po, "id");
        std::string name = Json::getStringField(po, "name");
        std::string loc = Json::getStringField(po, "location");
        int rows = Json::getIntField(po, "rows", 4);
        int cols = Json::getIntField(po, "cols", 6);
        std::cout << "Plot " << i << ": id=" << id << ", name=" << name << ", loc=" << loc << ", dims=" << rows << "x" << cols;
        if (dummyPlots.count(id)) {
            std::cout << " (SKIPPED DUMMY)\n";
            continue;
        }
        std::cout << " (KEPT)\n";

        // Gates
        size_t gatesPos = po.find("\"gates\"");
        if (gatesPos != std::string::npos) {
            size_t gatesOpen = po.find('[', gatesPos);
            if (gatesOpen != std::string::npos) {
                // Find matching close bracket for gates
                int depth = 0;
                size_t gatesClose = std::string::npos;
                for (size_t j = gatesOpen; j < po.length(); ++j) {
                    if (po[j] == '[') depth++;
                    else if (po[j] == ']') {
                        depth--;
                        if (depth == 0) {
                            gatesClose = j;
                            break;
                        }
                    }
                }
                if (gatesClose != std::string::npos) {
                    std::string gatesSub = po.substr(gatesOpen, gatesClose - gatesOpen + 1);
                    std::vector<std::string> gateObjs = splitJsonObjects(gatesSub);
                    std::cout << "  Gates: " << gateObjs.size() << "\n";
                    for (const auto& go : gateObjs) {
                        std::cout << "    - Gate: " << Json::getStringField(go, "name") 
                                  << " (" << Json::getStringField(go, "type") << ") at [" 
                                  << Json::getIntField(go, "row") << "," << Json::getIntField(go, "col") << "]\n";
                    }
                }
            }
        }

        // Grid
        size_t gridPos = po.find("\"grid\"");
        if (gridPos != std::string::npos) {
            size_t gridOpen = po.find('[', gridPos);
            if (gridOpen != std::string::npos) {
                // Find matching close bracket for outer grid
                int depth = 0;
                size_t gridClose = std::string::npos;
                for (size_t j = gridOpen; j < po.length(); ++j) {
                    if (po[j] == '[') depth++;
                    else if (po[j] == ']') {
                        depth--;
                        if (depth == 0) {
                            gridClose = j;
                            break;
                        }
                    }
                }
                if (gridClose != std::string::npos) {
                    std::string gridSub = po.substr(gridOpen, gridClose - gridOpen + 1);
                    std::vector<std::string> cellObjs = splitJsonObjects(gridSub);
                    std::cout << "  Cells: " << cellObjs.size() << " (Expected " << (rows * cols) << ")\n";
                }
            }
        }
    }

    std::cout << "\nTesting sessions.json:\n";
    std::ifstream sf("data/sessions.json");
    if (sf.is_open()) {
        std::stringstream sbuf;
        sbuf << sf.rdbuf();
        sf.close();
        std::vector<std::string> sessObjs = splitJsonObjects(sbuf.str());
        std::cout << "Found " << sessObjs.size() << " sessions\n";
        for (const auto& so : sessObjs) {
            std::cout << "  Session " << Json::getStringField(so, "sessionId") 
                      << ": plot=" << Json::getStringField(so, "plotId")
                      << ", slot=" << Json::getStringField(so, "slotId")
                      << ", plate=" << Json::getStringField(so, "vehiclePlate") << "\n";
        }
    }

    return 0;
}
