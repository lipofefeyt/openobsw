/*
 * MISRA canary for the static-analysis CI job. Not compiled.
 *
 * Deliberately breaks Required rule 15.6 (unbraced body). CI checks that
 * cppcheck reports it, so a misconfigured run (addon not loaded, style
 * severity filtered) fails instead of passing with zero findings.
 */
int misra_canary(int x);

int misra_canary(int x)
{
    int r = 0;
    if (x > 0)
        r = 1;
    return r;
}
