

import struct
import lzma

# File paths
input_txt_file = '/grand/hp-ptycho/binkma/dlrm_trace/trace2.txt'  # Replace with your actual file name
preprocessed_txt_file = '/grand/hp-ptycho/binkma/dlrm_trace/preprocessed_input2head.txt'
output_xz_file = '/grand/hp-ptycho/binkma/dlrm_trace/trace_output2head.xz'

# Instruction type for 'load' operation
LOAD_INST_TYPE = 1  # Assuming 'load' maps to value 1, based on your trace file format

# Access size set to a cache line size (64 bytes)
ACCESS_SIZE = 64
MAX_LINES = 30000000

# Function to preprocess input file and generate unique index IDs
def preprocess_input_file():
    unique_mapping = {}
    unique_index = 1
    line_count = sum(1 for _ in open(input_txt_file, 'r'))  # Count total lines for progress reporting
    current_line = 0

    with open(input_txt_file, 'r') as infile, open(preprocessed_txt_file, 'w') as outfile:
        for line in infile:
            if current_line >= MAX_LINES:
                break  # Stop processing after MAX_LINES
            try:
                emb_table_id, index_id = line.strip().split()
                emb_table_id = int(emb_table_id)
                index_id = int(index_id)
            except ValueError:
                print(f"Error parsing line: {line.strip()} - Skipping.")
                continue

            # Create a unique key for (embedding table ID, index ID)
            key = (emb_table_id, index_id)
            if key not in unique_mapping:
                unique_mapping[key] = unique_index
                unique_index += 1

            # Write the original data and the unique index to the output file
            outfile.write(f"{emb_table_id} {index_id} {unique_mapping[key]}\n")

            # Update progress
            current_line += 1
            if current_line % 1000 == 0 or current_line == line_count:
                print(f"Preprocessing progress: {current_line}/{line_count} lines processed")

            #if current_line == 5000000:
                
            #    break
        print("Preprocessing completed for 1 million lines.")


# Function to process a chunk of lines and return the packed data
def process_lines(lines):
    packed_data = bytearray()
    for line in lines:
        # Parse PC, EA, and unique index from each line (in decimal format)
        try:
            pc, ea, unique_index = line.strip().split()
            pc = int(pc)  # Assuming PC is given in decimal format
            ea = int(unique_index)  # Assuming EA is given in decimal format
        except ValueError:
            print(f"Error parsing line: {line.strip()} - Skipping.")
            continue

        # Ensure valid addresses
        if pc < 0 or ea < 0:
            print(f"Invalid PC or EA value: PC={pc}, EA={ea} - Skipping.")
            continue

        # Map EA to the first address of a cache line
        # Assuming EA represents an offset and we want to convert it to the start of a cache line
        ea = (ea * ACCESS_SIZE)  # Manipulate EA to map to the first address of the cache line

        # Check if the virtual address is 0x40
        if ea == 0x40:
            print(f"Virtual address is 0x40. pc value: {pc}")

        # Set up default values for other fields
        target = 0  # Default value for target (not applicable for load)
        taken = 0  # Not applicable for load, setting to 0
        num_input_regs = 0
        num_output_regs = 0
        input_reg_names = [0] * 256
        output_reg_names = [0] * 256
        output_reg_values = [[0, 0]] * 256

        # Define the instruction type as 'load'
        inst_type = LOAD_INST_TYPE

        # Pack the data into binary format
        packed_data.extend(struct.pack('<QQQBBBB', pc, ea, target, ACCESS_SIZE, taken, num_input_regs, num_output_regs))
        packed_data.extend(bytes(input_reg_names))  # Input register names
        packed_data.extend(bytes(output_reg_names))  # Output register names
        for reg_val in output_reg_values[:num_output_regs]:
            packed_data.extend(struct.pack('<QQ', *reg_val))  # Output register values

    return packed_data

# Function to generate ChampSim trace file
def generate_trace_file():
    try:
        # Preprocess the input file
        preprocess_input_file()

        # Read the preprocessed text file and create a compressed .xz file
        with open(preprocessed_txt_file, 'r') as infile:
            lines = infile.readlines()
            line_count = len(lines)
            chunk_size = 10000  # Process 10,000 lines at a time

            # Create the .xz file without threading for better stability
            with lzma.open(output_xz_file, 'wb') as xzfile:
                for i in range(0, line_count, chunk_size):
                    chunk = lines[i:i + chunk_size]
                    result = process_lines(chunk)
                    xzfile.write(result)

                    # Show progress
                    current_line = min((i + chunk_size), line_count)
                    print(f"Progress: {current_line}/{line_count} lines processed")

                    #if current_line == 5000000:
                    #    break
        print(f"Trace file generated and compressed as '{output_xz_file}'")
    except Exception as e:
        print(f"An error occurred while generating the trace file: {e}")

# Generate the trace file
generate_trace_file()