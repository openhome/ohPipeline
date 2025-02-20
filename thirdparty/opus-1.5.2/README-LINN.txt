NOTICE OF LINN CHANGHES

celt/mathops

Mathops defines isqrt32() which conflicts with a matching symbol in the WMA codec. 

This function is used inside the celt sub-library and appears to only be used at one point inside celt/bands.c. To resolve this conflict, the offending function was moved into bands.c and marked static so it wouldn't be exported to other consumers of the library. This may also prevent us from building the encoding portions of Opus code. However, as this is currently unused by us this is an acceptable tradeoff.