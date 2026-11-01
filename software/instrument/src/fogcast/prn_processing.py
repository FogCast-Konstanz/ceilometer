"""Host-side PRN correlation. Raw histogram remains the authoritative measurement."""
def sequence(length,seed):
    if length not in (1023,4095) or not 1<=seed<=length:
        raise ValueError("Invalid PRN length or seed")
    state=seed;code=[]
    for _ in range(length):
        code.append(state&1)
        feedback=state^(state>>3) if length==1023 else state^(state>>1)^(state>>4)^(state>>6)
        state=(state>>1)|((feedback&1)<<(9 if length==1023 else 11))
    return code

def process(histogram,seed,chip_hz,exposure_ns,offset_bins=0):
    import numpy as np
    n=len(histogram)
    if chip_hz<=0 or exposure_ns<=0 or any(v<0 or v>2**53 for v in histogram):
        raise ValueError("Invalid exposure/rate or histogram outside exact float64 integer range")
    h=np.asarray(histogram,dtype=np.float64)
    code=2*np.asarray(sequence(n,seed),dtype=np.float64)-1
    # Match firmware: positive delay peaks at positive lag. Divide firmware's
    # scaled integer result by N to obtain the DC-rejected count-unit profile.
    profile=np.fft.irfft(np.fft.rfft(h)*np.conj(np.fft.rfft(code)),n=n)-h.sum()/n
    seconds=exposure_ns/1e9
    ranges=(np.arange(n)-offset_bins)*299792458/(2*chip_hz)
    return {"range_m":ranges.tolist(),"correlation_counts":profile.tolist(),
            "correlation_counts_per_second":(profile/seconds).tolist()}
