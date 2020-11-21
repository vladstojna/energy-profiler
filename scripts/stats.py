import sys

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} [results file]")

    with open(sys.argv[1], "r") as f:
        total_time = 0
        pkg_energy = 0
        dram_energy = 0
        num_samples = 0
        for line in f:
            if line[0] == "#":
                continue
            num_samples += 1
            tokens = line.rstrip('\n').split(',')
            total_time += int(tokens[1])
            pkg_energy += float(tokens[2])
        seconds = total_time / 1e9
        print(f"File: {f.name}")
        print(f"Samples: {num_samples}")
        print(f"Total time: {seconds} s")
        print(f"Total package energy: {pkg_energy} J")
        print(f"Average package power: {pkg_energy / seconds} W")
        print(f"Total DRAM energy: {dram_energy} J")
        print(f"Average DRAM power: {dram_energy / seconds} W")

if __name__ == '__main__':
    main()
