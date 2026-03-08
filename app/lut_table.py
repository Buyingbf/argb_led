def print_gamma_lut():
    """Print a uint16_t Q8 gamma correction lookup table."""
    # Gamma value (typically 2.2 for displays)
    gamma = 2.2
    # Number of entries (256 for 8-bit input)
    entries = 256
    
    print("Gamma Correction LUT (Q8.8)")
    print("Index | Value")
    
    for i in range(entries):
        # Normalize input to 0.0-1.0
        normalized = i / (entries - 1)
        # Apply gamma correction
        corrected = normalized ** (gamma)
        # Convert to Q8 fixed point (0-65535 range for uint16)
        q8_value = int(corrected * 65535)
        
        print(f"{q8_value:5d}, ", end="")
        if (i % 8 == 7):  # Print 8 values per line
            print()  # New line after every 8 values

if __name__ == "__main__":
    print_gamma_lut()