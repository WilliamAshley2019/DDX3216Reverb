So I havn't actually reversed engineered the DDX3216 reverb on this but played with the
idea of applying some Sharc DSP processes for the all pass and instead of just using
the JUCE form of the Classic Schroeder reverb topology: y[n] = -g*x[n] + x[n-M] + g*y[n-M] 
with SIMD optimizations that are contained in the JUCE version to attempt to immitate
how the sharc hardware does it but in a real time safe (hopefully) method as the way
sharc processes and the way vst3 process is not as forgiving - as sharc processors operate
more like clap would allow rather than real time only audio processes in vst3. 
You may need to play around a bit with the values to get something workable but it was a
unique sounding reverb for me, it did have that digital feel to it but I dunno it just
sounds a little unique on my ears not like a simple reverb plugin or a really high end
reverb that sounds like real reverb, you know the reverb is fake but it has a digital feel
that is digitally trying to immitate the reverb rather than a plugin that just tries to imitate
reverb. So this is an attempt at "digital reverb emulation" rather than a plugin that provides
reverb.   All parameters match the original SysEx specification:
 The idea was to include some of the parameters in the original DDX3216 reverb.

 There was some confusion over the reverb being a feed forward reverb or a feedback reverb. 
 The conclusion was that old digital filters used feedback combs not feedforward combs so that 
 is why this reverb has a weird sound to it and you need to balancve it well to avoid feedback.
   

