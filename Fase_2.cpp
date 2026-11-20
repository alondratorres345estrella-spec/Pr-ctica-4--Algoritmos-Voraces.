#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <memory>
#include <cstdint>
#include <string>
#include <iomanip>

// ============================================================================
// 1. ESTRUCTURAS DEL ÁRBOL DE HUFFMAN
// ============================================================================
struct HuffmanNode {
    uint8_t character;
    uint64_t frequency;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    // Constructor para hoja
    HuffmanNode(uint8_t ch, uint64_t freq)
        : character(ch), frequency(freq), left(nullptr), right(nullptr) {}

    // Constructor para nodo interno
    HuffmanNode(std::shared_ptr<HuffmanNode> l, std::shared_ptr<HuffmanNode> r)
        : character(0), frequency(l->frequency + r->frequency), left(l), right(r) {}

    bool is_leaf() const {
        return left == nullptr && right == nullptr;
    }
};

struct NodeComparator {
    bool operator()(const std::shared_ptr<HuffmanNode>& a,
                    const std::shared_ptr<HuffmanNode>& b) const {
        return a->frequency > b->frequency; // Montículo de mínimos
    }
};

// ============================================================================
// 2. CLASES BITWRITER Y BITREADER (Empaquetado real a nivel de bits)
// ============================================================================
class BitWriter {
private:
    std::ofstream& out;
    uint8_t buffer;
    int bit_count;

public:
    explicit BitWriter(std::ofstream& stream)
        : out(stream), buffer(0), bit_count(0) {}

    // Escribe 1 bit (0 o 1) de izquierda a derecha (MSB a LSB)
    void write_bit(uint8_t bit) {
        buffer = static_cast<uint8_t>((buffer << 1) | (bit & 1));
        bit_count++;
        if (bit_count == 8) {
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bit_count = 0;
        }
    }

    // Escribe 8 bits completos (un byte)
    void write_byte(uint8_t byte) {
        for (int i = 7; i >= 0; --i) {
            write_bit((byte >> i) & 1);
        }
    }

    // Vacía los bits restantes agregando relleno de ceros (padding)
    void flush() {
        if (bit_count > 0) {
            buffer = static_cast<uint8_t>(buffer << (8 - bit_count));
            out.put(static_cast<char>(buffer));
            buffer = 0;
            bit_count = 0;
        }
    }
};

class BitReader {
private:
    std::ifstream& in;
    uint8_t buffer;
    int bit_count;

public:
    explicit BitReader(std::ifstream& stream)
        : in(stream), buffer(0), bit_count(0) {}

    // Lee 1 bit del flujo. Retorna false si se alcanza el fin de archivo
    bool read_bit(uint8_t& bit) {
        if (bit_count == 0) {
            char ch;
            if (!in.get(ch)) {
                return false;
            }
            buffer = static_cast<uint8_t>(ch);
            bit_count = 8;
        }
        bit = (buffer >> (bit_count - 1)) & 1;
        bit_count--;
        return true;
    }

    // Lee 8 bits consecutivos para reconstruir un carácter
    bool read_byte(uint8_t& byte) {
        byte = 0;
        for (int i = 0; i < 8; ++i) {
            uint8_t bit;
            if (!read_bit(bit)) {
                return false;
            }
            byte = static_cast<uint8_t>((byte << 1) | bit);
        }
        return true;
    }
};

// ============================================================================
// 3. CONSTRUCCIÓN, SERIALIZACIÓN Y TABLA DE CÓDIGOS
// ============================================================================
std::shared_ptr<HuffmanNode> build_huffman_tree(const std::vector<uint64_t>& frequencies) {
    std::priority_queue<std::shared_ptr<HuffmanNode>,
                        std::vector<std::shared_ptr<HuffmanNode>>,
                        NodeComparator> min_heap;

    for (int i = 0; i < 256; ++i) {
        if (frequencies.at(i) > 0) {
            min_heap.push(std::make_shared<HuffmanNode>(static_cast<uint8_t>(i), frequencies.at(i)));
        }
    }

    if (min_heap.empty()) return nullptr;

    // Caso borde: alfabeto con 1 solo símbolo único (ej. 10^7 letras 'A').
    // Se agrega una hoja ficticia como hermano derecho; el símbolo real
    // queda a la izquierda con código "0".
    if (min_heap.size() == 1) {
        auto single = min_heap.top();
        min_heap.pop();
        auto dummy = std::make_shared<HuffmanNode>(0, 0);
        return std::make_shared<HuffmanNode>(single, dummy);
    }

    while (min_heap.size() > 1) {
        auto left = min_heap.top(); min_heap.pop();
        auto right = min_heap.top(); min_heap.pop();
        auto parent = std::make_shared<HuffmanNode>(left, right);
        min_heap.push(parent);
    }

    return min_heap.top();
}

// Genera tabla de códigos usando vectores de bits
void generate_codes(const std::shared_ptr<HuffmanNode>& node,
                    std::vector<uint8_t>& current_code,
                    std::vector<std::vector<uint8_t>>& code_table,
                    bool is_dummy_branch = false) {
    if (!node) return;

    if (node->is_leaf()) {
        // Evita que la hoja ficticia (carácter 0) sobrescriba el código
        // real del byte 0x00 cuando ese es el único símbolo del archivo.
        if (!(is_dummy_branch && node->frequency == 0)) {
            code_table.at(node->character) = current_code;
        }
        return;
    }

    current_code.push_back(0);
    generate_codes(node->left, current_code, code_table, is_dummy_branch);
    current_code.pop_back();

    current_code.push_back(1);
    generate_codes(node->right, current_code, code_table, true);
    current_code.pop_back();
}

// Serialización del árbol en preorden (0 = nodo interno, 1 + 8 bits = hoja)
void serialize_tree(const std::shared_ptr<HuffmanNode>& node, BitWriter& writer) {
    if (!node) return;

    if (node->is_leaf()) {
        writer.write_bit(1);
        writer.write_byte(node->character);
    } else {
        writer.write_bit(0);
        serialize_tree(node->left, writer);
        serialize_tree(node->right, writer);
    }
}

// Reconstrucción del árbol desde el recorrido en preorden.
// Retorna nullptr si el archivo está truncado o corrupto.
std::shared_ptr<HuffmanNode> deserialize_tree(BitReader& reader) {
    uint8_t bit;
    if (!reader.read_bit(bit)) return nullptr;

    if (bit == 1) {
        uint8_t ch;
        if (!reader.read_byte(ch)) return nullptr;
        return std::make_shared<HuffmanNode>(ch, 0);
    } else {
        auto left = deserialize_tree(reader);
        if (!left) return nullptr;
        auto right = deserialize_tree(reader);
        if (!right) return nullptr;
        return std::make_shared<HuffmanNode>(left, right);
    }
}

// ============================================================================
// 4. RUTINAS DE COMPRESIÓN Y DESCOMPRESIÓN
// ============================================================================
bool compress_file(const std::string& input_path, const std::string& output_path) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        std::cerr << "Error: No se pudo abrir el archivo de entrada.\n";
        return false;
    }

    // Paso 1: Contar frecuencias y tamaño total original
    std::vector<uint64_t> frequencies(256, 0);
    uint64_t original_size = 0;
    char ch;
    while (in.get(ch)) {
        frequencies.at(static_cast<uint8_t>(ch))++;
        original_size++;
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: No se pudo crear el archivo comprimido.\n";
        return false;
    }

    // Guardar tamaño original exacto (64 bits) para ignorar el padding final
    out.write(reinterpret_cast<const char*>(&original_size), sizeof(original_size));

    if (original_size == 0) {
        std::cout << "Archivo vacio comprimido (0 bytes).\n";
        return true;
    }

    // Paso 2: Construir árbol y tabla de códigos
    auto root = build_huffman_tree(frequencies);
    std::vector<std::vector<uint8_t>> code_table(256);
    std::vector<uint8_t> current_code;
    generate_codes(root, current_code, code_table);

    // Paso 3: Escribir árbol serializado y datos comprimidos bit a bit
    BitWriter writer(out);
    serialize_tree(root, writer);

    in.clear();
    in.seekg(0, std::ios::beg);
    while (in.get(ch)) {
        const auto& bits = code_table.at(static_cast<uint8_t>(ch));
        for (uint8_t b : bits) {
            writer.write_bit(b);
        }
    }
    writer.flush();

    in.close();
    out.close();
    if (!out) {
        std::cerr << "Error: Fallo al escribir el archivo comprimido.\n";
        return false;
    }

    // Reporte de métricas para Benchmarking
    std::ifstream check_out(output_path, std::ios::binary | std::ios::ate);
    uint64_t compressed_size = static_cast<uint64_t>(check_out.tellg());
    double ratio = (static_cast<double>(compressed_size) / original_size) * 100.0;

    std::cout << "=== Compresion Exitosa ===\n"
              << "Bytes Originales  : " << original_size << " B\n"
              << "Bytes Comprimidos : " << compressed_size << " B\n"
              << "Ratio Compresion  : " << std::fixed << std::setprecision(2) << ratio << "%\n";
    return true;
}

bool decompress_file(const std::string& input_path, const std::string& output_path) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        std::cerr << "Error: No se pudo abrir el archivo comprimido.\n";
        return false;
    }

    // Leer tamaño original exacto (64 bits)
    uint64_t original_size = 0;
    if (!in.read(reinterpret_cast<char*>(&original_size), sizeof(original_size))) {
        std::cerr << "Error: Cabecera invalida.\n";
        return false;
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: No se pudo crear el archivo descomprimido.\n";
        return false;
    }

    if (original_size == 0) {
        std::cout << "Archivo vacio descomprimido.\n";
        return true;
    }

    // Reconstruir árbol desde la cabecera
    BitReader reader(in);
    auto root = deserialize_tree(reader);
    if (!root) {
        std::cerr << "Error: No se pudo reconstruir el arbol de Huffman.\n";
        return false;
    }

    // Decodificar exactamente 'original_size' bytes (ignora padding final)
    uint64_t decoded_count = 0;
    auto current = root;

    while (decoded_count < original_size) {
        uint8_t bit;
        if (!reader.read_bit(bit)) {
            std::cerr << "Error: Fin de archivo inesperado.\n";
            return false;
        }

        current = (bit == 0) ? current->left : current->right;
        if (!current) { // Archivo corrupto
            std::cerr << "Error: Datos comprimidos corruptos.\n";
            return false;
        }

        if (current->is_leaf()) {
            out.put(static_cast<char>(current->character));
            decoded_count++;
            current = root;
        }
    }

    in.close();
    out.close();
    std::cout << "=== Descompresion Exitosa ===\n"
              << "Bytes Restaurados : " << decoded_count << " B\n";
    return true;
}

// ============================================================================
// 5. FUNCIÓN PRINCIPAL (CLI)
// ============================================================================
int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);

    if (args.size() != 4) {
        std::cout << "Uso:\n"
                  << "  Comprimir   : " << args.at(0) << " -c <entrada> <salida.huff>\n"
                  << "  Descomprimir: " << args.at(0) << " -d <entrada.huff> <salida>\n";
        return 1;
    }

    if (args.at(1) == "-c") {
        return compress_file(args.at(2), args.at(3)) ? 0 : 1;
    } else if (args.at(1) == "-d") {
        return decompress_file(args.at(2), args.at(3)) ? 0 : 1;
    }

    std::cerr << "Opcion no reconocida. Use -c o -d.\n";
    return 1;
}