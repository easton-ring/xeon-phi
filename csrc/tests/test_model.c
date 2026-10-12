#include "check.h"
#include "knc/model.h"

int main(void)
{
    knc_model m;
    char err[128];
    double feat[2] = {3.0, -1.0}, scratch[16], logits[3];
    FILE *fp = fopen("test_model.tmp", "w");
    /* 2 features -> relu(2x2) -> 3 classes */
    fprintf(fp, "KNCMODEL 1\n2 3 2\n1 0\n2 1\n"
                "2 2 1\n1 0\n0 1\n0 0\n"
                "2 3 0\n1 0\n0 1\n1 1\n0 0 -5\n");
    fclose(fp);
    CHECK(knc_model_load(&m, "test_model.tmp", err, sizeof err) == 0);
    /* standardized: (1, -1) -> relu -> (1, 0) -> logits (1, 0, -4) */
    CHECK(knc_model_predict(&m, feat, scratch, logits) == 0);
    CHECK(logits[0] == 1.0 && logits[1] == 0.0 && logits[2] == -4.0);
    feat[0] = 1.0; feat[1] = 1.0; /* (0, 1) -> (0, 1) -> logits (0, 1, -4) */
    CHECK(knc_model_predict(&m, feat, scratch, logits) == 1);
    feat[1] = 0.0; /* (0, 0) -> logits (0, 0, -5): tie -> first index */
    CHECK(knc_model_predict(&m, feat, scratch, logits) == 0);
    knc_model_free(&m);
    fp = fopen("test_model.tmp", "w");
    fprintf(fp, "KNCMODEL 1\n2 3 1\n0 0\n1 1\n2 4 0\n");
    fclose(fp);
    CHECK(knc_model_load(&m, "test_model.tmp", err, sizeof err) != 0);
    CHECK(knc_model_load(&m, "missing.tmp", err, sizeof err) != 0);
    remove("test_model.tmp");
    return TEST_EXIT();
}
