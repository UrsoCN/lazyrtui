#include "lazyrtui/ftxui_converter.hpp"

#include <iostream>
#include <algorithm>

namespace lazyrtui {

ftxui::Color FTXUIConverter::parse_color(const std::string& color_name) {
    if (color_name == "red") return ftxui::Color::Red;
    if (color_name == "green") return ftxui::Color::Green;
    if (color_name == "yellow") return ftxui::Color::Yellow;
    if (color_name == "blue") return ftxui::Color::Blue;
    if (color_name == "magenta") return ftxui::Color::Magenta;
    if (color_name == "cyan") return ftxui::Color::Cyan;
    if (color_name == "white") return ftxui::Color::White;
    if (color_name == "black") return ftxui::Color::Black;
    if (color_name == "gray" || color_name == "grey") return ftxui::Color::GrayDark;
    if (color_name == "light_gray" || color_name == "light_grey") return ftxui::Color::GrayLight;
    
    // RGB hex support, e.g. "#FF0000"
    if (color_name.size() == 7 && color_name[0] == '#') {
        try {
            int r = std::stoi(color_name.substr(1, 2), nullptr, 16);
            int g = std::stoi(color_name.substr(3, 2), nullptr, 16);
            int b = std::stoi(color_name.substr(5, 2), nullptr, 16);
            return ftxui::Color::RGB(r, g, b);
        } catch (...) {}
    }
    return ftxui::Color::Default;
}

ftxui::Decorator FTXUIConverter::parse_style(const nlohmann::json& node) {
    ftxui::Decorator style = ftxui::nothing;

    if (node.contains("style") && node["style"].is_string()) {
        std::string s = node["style"].get<std::string>();
        if (s.find("bold") != std::string::npos) style = style | ftxui::bold;
        if (s.find("dim") != std::string::npos) style = style | ftxui::dim;
        if (s.find("italic") != std::string::npos) style = style | ftxui::italic;
        if (s.find("underline") != std::string::npos) style = style | ftxui::underlined;

        if (s.find("red") != std::string::npos) style = style | ftxui::color(ftxui::Color::Red);
        else if (s.find("green") != std::string::npos) style = style | ftxui::color(ftxui::Color::Green);
        else if (s.find("yellow") != std::string::npos) style = style | ftxui::color(ftxui::Color::Yellow);
        else if (s.find("blue") != std::string::npos) style = style | ftxui::color(ftxui::Color::Blue);
        else if (s.find("cyan") != std::string::npos) style = style | ftxui::color(ftxui::Color::Cyan);
        else if (s.find("magenta") != std::string::npos) style = style | ftxui::color(ftxui::Color::Magenta);
        else if (s.find("gray") != std::string::npos) style = style | ftxui::color(ftxui::Color::GrayDark);
    }

    if (node.value("bold", false)) style = style | ftxui::bold;
    if (node.value("dim", false)) style = style | ftxui::dim;
    if (node.value("italic", false)) style = style | ftxui::italic;
    if (node.value("underline", false)) style = style | ftxui::underlined;

    if (node.contains("color") && node["color"].is_string()) {
        style = style | ftxui::color(parse_color(node["color"].get<std::string>()));
    }
    if (node.contains("bgcolor") && node["bgcolor"].is_string()) {
        style = style | ftxui::bgcolor(parse_color(node["bgcolor"].get<std::string>()));
    }

    return style;
}

ftxui::Element FTXUIConverter::parse_canvas(const nlohmann::json& node) {
    int width = node.value("width", 40);
    int height = node.value("height", 20);

    return ftxui::canvas([node, width, height](ftxui::Canvas& c) {
        if (!node.contains("draw") || !node["draw"].is_array()) return;

        for (const auto& op : node["draw"]) {
            if (!op.is_object()) continue;
            std::string type = op.value("op", op.value("type", ""));

            ftxui::Color color = ftxui::Color::Default;
            if (op.contains("color") && op["color"].is_string()) {
                color = parse_color(op["color"].get<std::string>());
            }

            if (type == "line") {
                int x1 = op.value("x1", 0);
                int y1 = op.value("y1", 0);
                int x2 = op.value("x2", 0);
                int y2 = op.value("y2", 0);
                c.DrawPointLine(x1, y1, x2, y2, color);
            } else if (type == "point") {
                int x = op.value("x", 0);
                int y = op.value("y", 0);
                c.DrawPoint(x, y, true, color);
            } else if (type == "circle") {
                int x = op.value("x", 0);
                int y = op.value("y", 0);
                int r = op.value("radius", op.value("r", 5));
                c.DrawPointCircle(x, y, r, color);
            } else if (type == "text") {
                int x = op.value("x", 0);
                int y = op.value("y", 0);
                std::string content = op.value("content", op.value("text", ""));
                c.DrawText(x, y, content, [color](ftxui::Cell& cell) {
                    cell.foreground_color = color;
                });
            }
        }
    }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, height);
}

ftxui::Element FTXUIConverter::parse_node(const nlohmann::json& node) {
    if (node.is_string()) {
        return ftxui::text(node.get<std::string>());
    }

    if (node.is_array()) {
        ftxui::Elements children;
        for (const auto& item : node) {
            children.push_back(parse_node(item));
        }
        return ftxui::vbox(std::move(children));
    }

    if (!node.is_object()) {
        return ftxui::text("");
    }

    std::string type = node.value("type", "text");
    ftxui::Decorator style = parse_style(node);

    if (type == "text") {
        std::string content = node.value("content", node.value("text", ""));
        return ftxui::text(content) | style;
    }

    if (type == "hbox") {
        ftxui::Elements children;
        if (node.contains("children") && node["children"].is_array()) {
            for (const auto& child : node["children"]) {
                children.push_back(parse_node(child));
            }
        }
        return ftxui::hbox(std::move(children)) | style;
    }

    if (type == "vbox") {
        ftxui::Elements children;
        if (node.contains("children") && node["children"].is_array()) {
            for (const auto& child : node["children"]) {
                children.push_back(parse_node(child));
            }
        }
        return ftxui::vbox(std::move(children)) | style;
    }

    if (type == "separator") {
        return ftxui::separator() | style;
    }

    if (type == "filler") {
        return ftxui::filler();
    }

    if (type == "gauge") {
        float progress = node.value("progress", 0.0f);
        progress = std::max(0.0f, std::min(1.0f, progress));
        std::string dir = node.value("direction", "right");

        ftxui::Element g;
        if (dir == "left") g = ftxui::gaugeLeft(progress);
        else if (dir == "up") g = ftxui::gaugeUp(progress);
        else if (dir == "down") g = ftxui::gaugeDown(progress);
        else g = ftxui::gaugeRight(progress);

        return g | style;
    }

    if (type == "canvas") {
        return parse_canvas(node) | style;
    }

    // Fallback: render text content if present
    if (node.contains("text") || node.contains("content")) {
        std::string content = node.value("content", node.value("text", ""));
        return ftxui::text(content) | style;
    }

    return ftxui::text("") | style;
}

ftxui::Elements FTXUIConverter::parse_ui_spec(const nlohmann::json& spec) {
    ftxui::Elements els;
    if (spec.is_array()) {
        for (const auto& item : spec) {
            els.push_back(parse_node(item));
        }
    } else if (spec.is_object()) {
        els.push_back(parse_node(spec));
    } else if (spec.is_string()) {
        els.push_back(ftxui::text(spec.get<std::string>()));
    }
    return els;
}

} // namespace lazyrtui
