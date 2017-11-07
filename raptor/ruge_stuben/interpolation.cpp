// Copyright (c) 2015, Raptor Developer Team, University of Illinois at Urbana-Champaign
// License: Simplified BSD, http://opensource.org/licenses/BSD-2-Clause
#include "assert.h"
#include "core/types.hpp"
#include "interpolation.hpp"

using namespace raptor;

CSRMatrix* mod_classical_interpolation(CSRMatrix* A,
        CSRMatrix* S, const std::vector<int>& states)
{
    int startA, endA;
    int startS, endS;
    int start_k, end_k, col_k;
    int col, ctr, row;

    double weight;
    double weak_sum;
    double strong_sum;
    double diag;
    double val, val_k;
    double sign;
    std::vector<int> row_coarse;
    std::vector<double> row_coarse_sums;
    std::vector<double> row_strong;
    if (A->n_rows)
    {
        row_coarse.resize(A->n_rows, 0);
        row_coarse_sums.resize(A->n_rows, 0.0);
        row_strong.resize(A->n_rows, 0.0);
    }

    // Copy values of A into S

    A->sort();
    A->move_diag();
    S->sort();
    S->move_diag();
    CSCMatrix* AT = new CSCMatrix(A);
    AT->sort();
    AT->move_diag();
    
    std::vector<int> col_to_new;
    if (A->n_cols)
    {
        col_to_new.resize(A->n_cols, -1);
    }

    ctr = 0;
    for (int i = 0; i < A->n_cols; i++)
    {
        if (states[i])
        {
            col_to_new[i] = ctr++;
        }
    }

    // Form P
    CSRMatrix* P = new CSRMatrix(A->n_rows, ctr, A->nnz);

    // Main loop.. add entries to P
    P->idx1[0] = 0;
    for (int i = 0; i < A->n_rows; i++)
    {
        if (states[i])
        {
            P->idx2.push_back(col_to_new[i]);
            P->vals.push_back(1.0);
            P->idx1[i+1] = P->idx2.size();
            continue;
        }

        startA = A->idx1[i];
        endA = A->idx1[i+1];
        startS = S->idx1[i];
        endS = S->idx1[i+1];

        // Skip over diagonal values
        if (S->idx2[startS] == i)
        {
            startS++;
        }
        if (A->idx2[startA] == i)
        {
            diag = A->vals[startA];
            startA++;
        }
        else
        {
            diag = 0.0;
        }
        ctr = startS;
        weak_sum = 0.0;

        if (diag > 0)
        {
            sign = 1.0;
        }
        else
        {
            sign = -1.0;
        }

        // Find weak sum, and save coarse cols / strong col values
        for (int j = startA; j < endA; j++)
        {
            col = A->idx2[j];
            if (ctr < endS && S->idx2[ctr] == col) // Strong
            {
                if (states[col])
                {
                    row_coarse[col] = 1;
                }
                else
                {
                    row_strong[col] = A->vals[j];
                }
                ctr += 1;
            }
            else // Weak
            {
                weak_sum += A->vals[j];
            }
        }

        // Find row coarse sums 
        for (int j = startS; j < endS; j++)
        {
            col = S->idx2[j];
            if (states[col] == 0)
            {
                start_k = A->idx1[col];
                end_k = A->idx1[col+1];
                if (A->idx2[start_k] == col)
                {
                    start_k++;
                }
                for (int k = start_k; k < end_k; k++)
                {
                    col_k = A->idx2[k];
                    val_k = A->vals[k];
                    if (row_coarse[col_k] && val_k * sign < 0)
                    {
                        row_coarse_sums[col] += val_k;
                    }
                }
                if (row_coarse_sums[col] == 0.0)
                {
                    weak_sum += S->vals[j];
                }
            }
        }
        weak_sum += diag;

        // Find weight for all coarse cols
        for (int j = startS; j < endS; j++)
        {
            col = S->idx2[j];
            if (states[col] == 1)
            {
                strong_sum = -S->vals[j];;
                start_k = AT->idx1[col];
                end_k = AT->idx1[col+1];
                if (AT->idx2[start_k] == col)
                {
                    start_k++;
                }
                for (int k = start_k; k < end_k; k++)
                {
                    row = AT->idx2[k];
                    val = AT->vals[k];
                    if (row_coarse_sums[row] && val * sign < 0)
                    {
                        strong_sum -= ((row_strong[row] * val) 
                                / row_coarse_sums[row]);
                    }
                }
                weight = strong_sum / weak_sum;
                P->idx2.push_back(col_to_new[col]);
                P->vals.push_back(weight);
            }
        }

        // Reset row coarse and row strong
        for (int j = startS; j < endS; j++)
        {
            col = S->idx2[j];
            row_strong[col] = 0;
            row_coarse[col] = 0.0;
            row_coarse_sums[col] = 0.0;
        }
            
        P->idx1[i+1] = P->idx2.size();
    }
    P->nnz = P->idx2.size();

    delete AT;
    return P;
}

CSRMatrix* direct_interpolation(CSRMatrix* A,
        CSRMatrix* S, const std::vector<int>& states)
{
    int start, end, col;
    int idx, new_idx, ctr;
    double sum_strong_pos, sum_strong_neg;
    double sum_all_pos, sum_all_neg;
    double val, alpha, beta, diag;
    double neg_coeff, pos_coeff;

    A->sort();
    S->sort();
    A->move_diag();
    S->move_diag();

    std::vector<int> col_to_new;
    if (A->n_cols)
    {
        col_to_new.resize(A->n_cols, -1);
    }

    ctr = 0;
    for (int i = 0; i < A->n_cols; i++)
    {
        if (states[i])
        {
            col_to_new[i] = ctr++;
        }
    }

    CSRMatrix* P = new CSRMatrix(A->n_rows, ctr, A->nnz);

    P->idx1[0] = 0;
    for (int i = 0; i < A->n_rows; i++)
    {
        if (states[i] == 1)
        {
            P->idx2.push_back(col_to_new[i]);
            P->vals.push_back(1);
        }
        else
        {
            sum_strong_pos = 0;
            sum_strong_neg = 0;
            sum_all_pos = 0;
            sum_all_neg = 0;

            start = S->idx1[i];
            end = S->idx1[i+1];
            if (S->idx2[start] == i)
            {
                start++;
            }
            for (int j = start; j < end; j++)
            {
                col = S->idx2[j];
                if (states[col] == 1)
                {
                    val = S->vals[j];
                    if (val < 0)
                    {
                        sum_strong_neg += val;
                    }
                    else
                    {
                        sum_strong_pos += val;
                    }
                }
            }

            start = A->idx1[i];
            end = A->idx1[i+1];
            diag = A->vals[start];
            for (int j = start+1; j < end; j++)
            {
                val = A->vals[j];
                if (val < 0)
                {
                    sum_all_neg += val;
                }
                else
                {
                    sum_all_pos += val;
                }
            }

            alpha = sum_all_neg / sum_strong_neg;

            if (sum_strong_pos == 0)
            {
                diag += sum_all_pos;
                beta = 0;
            }
            else
            {
                beta = sum_all_pos / sum_strong_pos;
            }

            neg_coeff = -alpha / diag;
            pos_coeff = -beta / diag;

            start = S->idx1[i];
            end = S->idx1[i+1];
            if (S->idx2[start] == i)
            {
                start++;
            }
            for (int j = start; j < end; j++)
            {
                col = S->idx2[j];
                if (states[col] == 1)
                {
                    val = S->vals[j];
                    P->idx2.push_back(col_to_new[col]);
                    if (val < 0)
                    {
                        P->vals.push_back(neg_coeff * val);
                    }
                    else
                    {
                        P->vals.push_back(pos_coeff * val);
                    }
                }
            }
        }
        P->idx1[i+1] = P->idx2.size();
    }
    P->nnz = P->idx2.size();

    return P;



}




