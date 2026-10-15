/*
 * Minimal freestanding-ish object-code probe for Knights Corner.
 * This is deliberately tiny: it does not need MPSS headers or libraries.
 * Object metadata, not successful compilation alone, determines the target.
 */
volatile unsigned long knc_probe_value;

int knc_probe_add(int a, int b)
{
    knc_probe_value = (unsigned long)(a + b);
    return a + b;
}

int main(void)
{
    return knc_probe_add(2, 3) == 5 ? 0 : 1;
}
