/* Vectorization probe: compiled to assembly only. On KNC, use of the 512-bit
 * vector unit (IMCI) shows up as %zmm registers in the generated assembly. */
void knc_saxpy(float *restrict y, const float *restrict x, float a, int n)
{
    int i;
    for (i = 0; i < n; i++)
        y[i] = a * x[i] + y[i];
}
