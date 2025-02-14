NOTICE OF LINN CHANGHES

celt/mathops

Mathops defines isqrt32() which conflicts with a matching symbol in the WMA codec. As this function is only used in the encoding side of Opus, it has been commented out so not to be included. This solves the building conflict. Happy days.