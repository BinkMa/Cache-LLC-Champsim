startline = 1000000  # Start line number
maxlines = 25000000+startline  # Maximum number of lines to process
def process_file(filename):
    line_count = 0
    max_third_column = float('-inf')  # Initialize with negative infinity

    with open(filename, 'r') as file:
        for line in file:
            if line_count > maxlines:
                break 
            line_count += 1
            columns = line.strip().split()
            if len(columns) >= 3:
                third_column_value = int(columns[2])
                max_third_column = max(max_third_column, third_column_value)

    return max_third_column

# Usage
filename = '/grand/hp-ptycho/binkma/dlrm_trace/preprocessed_input1.txt'  # Replace with your actual file name
max_value = process_file(filename)

# print(f"Number of lines: {maxlines}")
print(f"Maximum value in the third column: {max_value}")
