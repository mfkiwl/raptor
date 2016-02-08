#include "matmult.hpp"


/**************************************************************
 *****   Create MPI Type
 **************************************************************
 ***** Creates a custom MPI_Datatype for communincating
 ***** COO elements (struct Element)
 *****
 ***** Parameters
 ***** -------------
 ***** coo_type : MPI_Datatype*
 *****    MPI_Datatype to be returned
 **************************************************************/
void create_mpi_type(MPI_Datatype* coo_type)
{
    int blocks[2] = {2, 1};
    MPI_Datatype types[2] = {MPI_INDEX_T, MPI_DATA_T};
    MPI_Aint displacements[2];
    MPI_Aint intex;
    MPI_Type_extent(MPI_INDEX_T, &intex);
    displacements[0] = static_cast<MPI_Aint>(0);
    displacements[1] = 2*intex;
    MPI_Type_struct(2, blocks, displacements, types, coo_type);
}

/**************************************************************
 *****   Map To Global
 **************************************************************
 ***** Calculates the dot product of two sparse vectors
 ***** alpha = u^T*v
 *****
 ***** Parameters
 ***** -------------
 ***** i : index_t
 *****    Index to be mapped from local to global
 ***** map : std::vector<index_t>
 *****    Vector that maps from local to global
 *****
 ***** Returns
 ***** -------------
 ***** index_t :
 *****    Global index of i
 **************************************************************/
index_t map_to_global(index_t i, std::vector<index_t> map)
{
    return map[i];
}

/**************************************************************
 *****   Map To Global
 **************************************************************
 ***** Calculates the dot product of two sparse vectors
 ***** alpha = u^T*v
 *****
 ***** Parameters
 ***** -------------
 ***** i : index_t
 *****    Index to be mapped from local to global
 ***** map : index_t
 *****    Additional integer to map local to global
 *****
 ***** Returns
 ***** -------------
 ***** index_t :
 *****    Global index of i
 **************************************************************/
index_t map_to_global(index_t i, index_t addition)
{
    return i + addition;
}

/**************************************************************
 *****   Dot Product
 **************************************************************
 ***** Calculates the dot product of two sparse vectors
 ***** alpha = u^T*v
 *****
 ***** Parameters
 ***** -------------
 ***** size_u : index_t
 *****    Number of elements in vector u
 ***** size_v : index_t 
 *****    Number of elements in vector v
 ***** local_u : index_t* 
 *****    Indices of nonzeros in vector u
 ***** local_v : index_t*
 *****    Indices of nonzeros in vector v
 ***** data_u : data_t*
 *****    Values of nonzeros in vector u
 ***** data_v : data_t*
 *****    Values of nonzeros in vector v
 ***** map_u : UType
 *****    Maps indices of u from local to global
 ***** map_v : VType
 *****    Maps indices of v from local to global
 **************************************************************/
template <typename UType, typename VType>
data_t dot(index_t size_u, index_t size_v, index_t* local_u, 
    index_t* local_v, data_t* data_u, data_t* data_v,
    UType map_u, VType map_v)
{
    index_t ctr_u = 0;
    index_t ctr_v = 0;
    data_t result = 0.0;

    // Get index of first nonzero in u and v
    index_t k_u = map_to_global(local_u[ctr_u], map_u);
    index_t k_v = map_to_global(local_v[ctr_v], map_v);
    while (ctr_u < size_u && ctr_v < size_v)
    {
        // If indices of nonzeros are equal, add to product
        if (k_u == k_v)
        {
            result += data_u[ctr_u++] * data_v[ctr_v++];
            
            // If next element exists in each vector,
            // find index of next element
            if (ctr_u < size_u && ctr_v < size_v)
            {
                k_u = map_to_global(local_u[ctr_u], map_u);
                k_v = map_to_global(local_v[ctr_v], map_v);
            }
        }

        // If index of u > index of v, look at next in v
        else if (k_u > k_v)
        {
            ctr_v++;
            if (ctr_v < size_v)
            {
                k_v = map_to_global(local_v[ctr_v], map_v);
            }
        }

        // If index of v > index of u, look at next in u
        else
        {
            ctr_u++;
            if (ctr_u < size_u)
            {
                k_u = map_to_global(local_u[ctr_u], map_u);
            }
        }
    }
    return result;
}

/**************************************************************
 *****   Dot Product
 **************************************************************
 ***** Pulls rows/columns from matrices, and computes
 ***** dot product of these
 *****
 ***** Parameters
 ***** -------------
 ***** A : Matrix*
 *****    Matrix to extract row from ( u )
 ***** B : Matrix*
 *****    Matrix to extract column from ( v )
 ***** map_A : AType
 *****    Maps local indices of A to global
 ***** map_B : BType
 *****    Maps local indices of B to global
 ***** A_start : index_t 
 *****    Index of first element in column of A
 ***** A_end : index_t 
 *****    Index of first element in next column of A
 ***** B_start : index_t
 *****    Index of first element in row of B
 ***** B_end : index_t
 *****    Index of first element in next row of B
 **************************************************************/
template <typename AType, typename BType>
data_t dot(Matrix* A, Matrix* B, AType map_A, BType map_B, 
        index_t A_start, index_t A_end,
        index_t B_start, index_t B_end)
{
    index_t size_A = A_end - A_start;
    index_t size_B = B_end - B_start;

    index_t* local_A = &(A->indices[A_start]);
    index_t* local_B = &(B->indices[B_start]);

    data_t* data_A = &(A->data[A_start]);
    data_t* data_B = &(B->data[B_start]);

    return dot<AType, BType>(size_A, size_B, local_A, local_B,
        data_A, data_B, map_A, map_B);
}

/**************************************************************
 *****   Partial Sequential Matrix-Matrix Multiplication
 **************************************************************
 ***** Performs a partial matmult, multiplying Matrix A
 ***** by a single column of B
 *****
 ***** Parameters
 ***** -------------
 ***** A : Matrix*
 *****    Matrix to be multipled (on left)
 ***** B : Matrix*
 *****    Matrix to have single column multiplied (on right)
 ***** C : ParMatrix*
 *****    Parallel Matrix result is added to
 ***** map_A : AType
 *****    Maps local rows of A to global rows 
 ***** map_B : BType 
 *****    Maps local columns of B to global columns
 ***** map_C : CType
 *****    Maps local resulting column to global
 ***** col : index_t 
 *****    Column of B to be multiplied
 **************************************************************/
template <typename AType, typename BType, typename CType>
void seq_mm(Matrix* A, Matrix* B, ParMatrix* C, AType map_A,
        BType map_B, CType map_C, index_t col)
{
    index_t B_start = B->indptr[col];
    index_t B_end = B->indptr[col+1];
    for (index_t row = 0; row < A->n_rows; row++)
    {
        index_t A_start = A->indptr[row];
        index_t A_end = A->indptr[row+1];

        data_t cij = dot<AType, BType> (A, B, map_A,
             map_B, A_start, A_end, B_start, B_end);
        index_t global_col = map_to_global(col, map_C);
        C->add_value(row, global_col, cij);
    }
}

/**************************************************************
 *****   Sequential Matrix-Matrix Multiplication
 **************************************************************
 ***** Performs matrix-matrix multiplication A*B
 *****
 ***** Parameters
 ***** -------------
 ***** A : Matrix*
 *****    Matrix to be multipled (on left)
 ***** B : Matrix*
 *****    Matrix to be multiplied (on right)
 ***** C : ParMatrix*
 *****    Parallel Matrix result is added to
 ***** map_A : AType
 *****    Maps local rows of A to global rows 
 ***** map_B : BType 
 *****    Maps local columns of B to global columns
 ***** map_C : CType
 *****    Maps local resulting column to global
 **************************************************************/
template <typename AType, typename BType, typename CType>
void seq_mm(Matrix* A, Matrix* B, ParMatrix* C, AType map_A,
        BType map_B, CType map_C)
{
    data_t cij;
    index_t global_col;

    for (index_t col = 0; col < B->n_cols; col++)
    {
        seq_mm<AType, BType, CType> (A, B, C, map_A,
            map_B, map_C, col);
    }
}

/**************************************************************
 *****   Partial Sequential Transpose Matrix-Matrix Multiplication
 **************************************************************
 ***** Performs a partial transpose matmult, multiplying Matrix A
 ***** by a single column of B
 *****
 ***** Parameters
 ***** -------------
 ***** A : Matrix*
 *****    Matrix to be multipled (on right)
 ***** B : Matrix*
 *****    Matrix to have single transpose column multiplied (on left)
 ***** C : ParMatrix*
 *****    Parallel Matrix result is added to
 ***** map_row_A : AType
 *****    Maps local rows of A to global rows 
 ***** map_row_B : BType 
 *****    Maps local rows of B to global rows
 ***** map_col_A : CType
 *****    Maps local cols of A to global cols 
 ***** colB : index_t 
 *****    Column of B to be multiplied
 **************************************************************/
template <typename AType, typename BType, typename CType>
void seq_mm_T(Matrix* A, Matrix* B, ParMatrix* C, AType map_row_A,
        BType map_row_B, CType map_col_A, index_t colB)
{
    index_t colB_start = B->indptr[colB];
    index_t colB_end = B->indptr[colB+1];
    for (index_t colA = 0; colA < A->n_cols; colA++)
    {
        index_t colA_start = A->indptr[colA];
        index_t colA_end = A->indptr[colA+1];

        data_t cij = dot<AType, BType> (A, B, map_row_A,
             map_row_B, colA_start, colA_end, colB_start, colB_end);
        index_t global_row = map_to_global(colA, map_col_A);
        C->add_value(colB, global_row, cij);
    }
}

/**************************************************************
 *****   Partial Sequential Transpose Matrix-Matrix Multiplication
 **************************************************************
 ***** Performs a partial transpose matmult, multiplying Matrix A
 ***** by a single column of B
 *****
 ***** Parameters
 ***** -------------
 ***** A : Matrix*
 *****    Matrix to be multipled (on right)
 ***** B : Matrix*
 *****    Matrix to have single transpose column multiplied (on left)
 ***** C : ParMatrix*
 *****    Parallel Matrix result is added to
 ***** map_row_A : AType
 *****    Maps local rows of A to global rows 
 ***** map_row_B : BType 
 *****    Maps local rows of B to global rows
 ***** map_col_A : CType
 *****    Maps local cols of A to global cols 
 **************************************************************/
template <typename AType, typename BType, typename CType>
void seq_mm_T(Matrix* A, Matrix* B, ParMatrix* C, AType map_row_A,
        BType map_row_B, CType map_col_A)
{
    data_t cij;
    index_t global_col;

    for (index_t colB = 0; colB < B->n_cols; colB++)
    {
        seq_mm_T<AType, BType, CType> (A, B, C, map_row_A,
            map_row_B, map_col_A, colB);
    }
}

/**************************************************************
 *****   Parallel Matrix - Matrix Multiplication
 **************************************************************
 ***** Multiplies together two parallel matrices, outputing
 ***** the result in a new ParMatrix
 *****
 ***** Parameters
 ***** -------------
 ***** A : ParMatrix*
 *****    Parallel matrix to be multiplied (on left)
 ***** B : ParMatrix* 
 *****    Parallel matrix to be multiplied (on right)
 ***** _C : ParMatrix**
 *****    Parallel Matrix result is inserted into
 **************************************************************/
void parallel_matmult(ParMatrix* A, ParMatrix* B, ParMatrix** _C)
{
/*    // If process not active, create new 
    // empty matrix, and return
    if (!(A->local_rows))
    {
        *_C = new ParMatrix();
        return;
    }

    // Declare matrix variables
    ParMatrix* C;
    Matrix* A_diag;
    Matrix* A_offd;
    Matrix* B_diag;
    Matrix* B_offd;

    // Declare format variables
    format_t format_ad;
    format_t format_ao;
    format_t format_bd;
    format_t format_bo;

    // Declare communication variables
    ParComm* comm;
    std::vector<index_t> send_procs;
    std::vector<index_t> recv_procs;
    std::map<index_t, std::vector<index_t>> send_indices;
    std::map<index_t, std::vector<index_t>> recv_indices;
    MPI_Request* send_requests;
    MPI_Status recv_status;
    std::vector<Element>* send_buffer;
    int send_proc;
    index_t num_sends;
    index_t num_recvs;
    int count;
    int n_recv;
    int avail_flag;

    // Declare matrix helper variables
    index_t row_start;
    index_t row_end;
    index_t col_start;
    index_t col_end;
    index_t local_row;
    index_t local_col;
    index_t global_row;
    index_t global_col;
    data_t value;
    Element tmp;
    index_t num_cols;

    // Declare temporary (recvd) matrix variables
    Matrix* Btmp;
    std::map<index_t, index_t> global_to_local;
    std::vector<index_t> local_to_global;
    std::vector<index_t> col_nnz;
    index_t tmp_size;

    // Initialize matrices
    C = new ParMatrix(A->global_rows, B->global_cols, A->comm_mat);
    A_diag = A->diag;
    A_offd = A->offd;
    B_diag = B->diag;
    B_offd = B->offd;

    // Set initial formats (to return with same type)
    format_ad = A_diag->format;
    format_bd = B_diag->format;
    if (A->offd_num_cols)
    {
        format_ao = A_offd->format;
    }
    if (B->offd_num_cols)
    {
        format_bo = B_offd->format;
    }

    // Initialize Communication Package
    comm = A->comm;
    send_procs = comm->send_procs;
    recv_procs = comm->recv_procs;
    send_indices = comm->send_indices;
    recv_indices = comm->recv_indices;
    send_requests = new MPI_Request[send_procs.size()];
    send_buffer = new std::vector<Element>[send_procs.size()];
    num_sends = send_procs.size();
    num_recvs = recv_procs.size();

    // Send elements of B as array of COO structs 
    B_diag->convert(CSR);
    if (B->offd_num_cols)
    {
        B_offd->convert(CSR);
    }

    // Create custom datatype (COO elements)
    MPI_Datatype coo_type;
    create_mpi_type(&coo_type);

    // Commit COO Datatype
    MPI_Type_commit(&coo_type);

    // Send B-values to necessary processors
    for (index_t i = 0; i < num_sends; i++)
    {
        send_proc = send_procs[i];
        std::vector<index_t> send_idx = send_indices[send_proc];
        for (auto row : send_idx)
        {
            // Send diagonal portion of row
            row_start = B_diag->indptr[row];
            row_end = B_diag->indptr[row+1];
            for (index_t j = row_start; j < row_end; j++)
            {
                local_col = B_diag->indices[j] + B->first_col_diag;
                value = B_diag->data[j];
                tmp = {row + B->first_row, local_col, value};
                send_buffer[i].push_back(tmp);
            }

            // Send off-diagonal portion of row
            if (B->offd_num_cols)
            {
                row_start = B_offd->indptr[row];
                row_end = B_offd->indptr[row+1];
                for (index_t j = row_start; j < row_end; j++)
                {
                    local_col = B->local_to_global[B_offd->indices[j]];
                    value = B_offd->data[j];
                    tmp = {row + B->first_row, local_col, value};
                    send_buffer[i].push_back(tmp);
                }
            }
        }
        // Send B-values to distant processor
        MPI_Isend(send_buffer[i].data(), send_buffer[i].size(), coo_type,
            send_proc, 1111, A->comm_mat, &send_requests[i]);
    }

    // Convert A to CSR, B to CSC, and multiply local portion
    A_diag->convert(CSR);
    if (A->offd_num_cols)
    {
        A_offd->convert(CSR);
    }
    B_diag->convert(CSC);
   
    // Multiply A_diag * B_diag
    seq_mm<index_t, index_t, index_t>(A_diag, B_diag, C, 
        A->first_col_diag, B->first_row, B->first_col_diag);

    if (B->offd_num_cols)
    {
        B_offd->convert(CSC);
        
        // Multiply A_diag * B_offd
        seq_mm<index_t, index_t, std::vector<index_t>>(A_diag, B_offd, C, A->first_col_diag, B->first_row, B->local_to_global);
    }

    // Receive messages and multiply
    count = 0;
    n_recv = 0;
    while (n_recv < num_recvs)
    {
        //Probe for messages, and recv any found
        MPI_Iprobe(MPI_ANY_SOURCE, 1111, A->comm_mat, &avail_flag, &recv_status);
        if (avail_flag)
        {
            // Get size of message in buffer
            MPI_Get_count(&recv_status, coo_type, &count);

            // Get process message comes from
            index_t proc = recv_status.MPI_SOURCE;

            // Receive message into buffer
            Element recv_buffer[count];
            MPI_Recv(&recv_buffer, count, coo_type, MPI_ANY_SOURCE, 1111, A->comm_mat, &recv_status);

            // Initialize matrix for received values
            num_cols = 0;
            tmp_size = recv_indices[proc].size();
            Btmp = new Matrix(tmp_size, tmp_size);

            // Add values to Btmp
            for (index_t i = 0; i < count; i++)
            {
                global_row = recv_buffer[i].row;
                global_col = recv_buffer[i].col;
                value = recv_buffer[i].value;

                local_row = A->global_to_local[global_row];
                if (global_to_local.count(global_col) == 0)
                {
                    global_to_local[global_col] = col_nnz.size();
                    local_to_global.push_back(global_col);
                    col_nnz.push_back(1);
                    num_cols++;
                }
                else
                {
                    col_nnz[global_to_local[global_col]]++;
                }
                Btmp->add_value(local_row, global_to_local[global_col], value);
            }

            // Finalize Btmp
            Btmp->resize(tmp_size, num_cols);
            Btmp->finalize(CSC);

            //Multiply one col at a time
            for (index_t col = 0; col < num_cols; col++)
            {
                seq_mm<std::vector<index_t>, std::vector<index_t>, std::vector<index_t>>
                    (A->offd, Btmp, C, A->local_to_global, A->local_to_global,
                        local_to_global, col);
            }

            // Delete Btmp
            global_to_local.clear();
            local_to_global.clear();
            col_nnz.clear();
            delete Btmp;

            // Increment number of recvs
            n_recv++;
        }
    }

    // Free custom COO datatype (finished communication)
    MPI_Type_free(&coo_type);

    // Convert matrices back to original format
    A_diag->convert(format_ad);
    if (A->offd_num_cols)
    {
        A_offd->convert(format_ao);
    }
    B_diag->convert(format_bd);
    if (B->offd_num_cols)
    {
        B_offd->convert(format_bo);
    }

    // Finalize parallel output matrix
    C->finalize(0);
    *_C = C;

    // Wait for sends to finish, then delete buffer
    if (num_sends)
    {
	    MPI_Waitall(send_procs.size(), send_requests, MPI_STATUS_IGNORE);
        delete[] send_buffer;
        delete[] send_requests; 
    }*/
} 

/**************************************************************
 *****   Parallel Transpose Matrix - Matrix Multiplication
 **************************************************************
 ***** Multiplies together two parallel matrices , outputing
 ***** the result in a new ParMatrix C = B^T*A
 *****
 ***** Parameters
 ***** -------------
 ***** A : ParMatrix*
 *****    Parallel matrix to be multiplied (on right)
 ***** B : ParMatrix* 
 *****    Parallel matrix to be transpose multiplied (on left)
 ***** _C : ParMatrix**
 *****    Parallel Matrix result is inserted into
 **************************************************************/
void parallel_matmult_T(ParMatrix* A, ParMatrix* B, ParMatrix** _C)
{
/*    // If process not active, create new 
    // empty matrix, and return
    if (!(A->local_rows))
    {
        *_C = new ParMatrix();
        return;
    }

    // Declare matrix variables
    ParMatrix* C;
    Matrix* A_diag;
    Matrix* A_offd;
    Matrix* B_diag;
    Matrix* B_offd;

    // Declare format variables
    format_t format_ad;
    format_t format_ao;
    format_t format_bd;
    format_t format_bo;

    // Declare communication variables
    ParComm* comm;
    std::vector<index_t> send_procs;
    std::vector<index_t> recv_procs;
    std::map<index_t, std::vector<index_t>> send_indices;
    std::map<index_t, std::vector<index_t>> recv_indices;
    MPI_Request* send_requests;
    MPI_Status recv_status;
    std::vector<Element>* send_buffer;
    index_t send_proc;
    index_t num_sends;
    index_t num_recvs;
    int count;
    index_t n_recv;
    int avail_flag;

    // Declare matrix helper variables
    index_t colA_start;
    index_t colA_end;
    index_t colB_start;
    index_t colB_end;
    index_t local_row;
    index_t local_col;
    index_t global_row;
    index_t global_col;
    data_t value;
    Element tmp;
    index_t num_cols;

    // Declare temporary (recvd) matrix variables
    Matrix* Btmp;
    std::map<index_t, index_t> global_to_local;
    std::vector<index_t> local_to_global;
    std::vector<index_t> col_nnz;
    index_t tmp_size;

    // Initialize matrices
    C = new ParMatrix(B->global_cols, A->global_cols, B->comm_mat);
    A_diag = A->diag;
    A_offd = A->offd;
    B_diag = B->diag;
    B_offd = B->offd;

    // Set initial formats (to return with same type)
    format_ad = A_diag->format;
    format_bd = B_diag->format;
    if (A->offd_num_cols)
    {
        format_ao = A_offd->format;
    }
    if (B->offd_num_cols)
    {
        format_bo = B_offd->format;
    }

    // Initialize Communication Package
    comm = A->comm;
    send_procs = comm->recv_procs;
    recv_procs = comm->send_procs;
    send_indices = comm->recv_indices;
    recv_indices = comm->send_indices;
    num_sends = send_procs.size();
    num_recvs = recv_procs.size();
    send_requests = new MPI_Request[num_sends];
    send_buffer = new std::vector<Element>[num_sends];

    // Convert all matrices to CSC for multiplication
    A_diag->convert(CSC);
    if (A->offd_num_cols)
    {
        A_offd->convert(CSC);
    }
    B_diag->convert(CSC);
    if (B->offd_num_cols)
    {
        B_offd->convert(CSC);
    }

    // Create custom datatype (COO elements)
    MPI_Datatype coo_type;
    create_mpi_type(&coo_type);

    // Commit COO Datatype
    MPI_Type_commit(&coo_type);

    // Send B-values to necessary processors
    for (index_t i = 0; i < num_sends; i++)
    {
        send_proc = send_procs[i];
        std::vector<index_t> send_idx = send_indices[send_proc];
        if (B->offd_num_cols)
        {
            for (auto colB : send_idx)
            {
                colB_start = B_offd->indptr[colB];
                colB_end = B_offd->indptr[colB+1];
            
                if (colB_start < colB_end)
                {
                    for (index_t colA = 0; colA < A_diag->n_cols; colA++)
                    {
                        colA_start = A_diag->indptr[colA];
                        colA_end = A_diag->indptr[colA+1];
                    
                        if (colA_start < colA_end)
                        {
                            value = dot<index_t, index_t>(A_diag, B_offd, A->first_row, B->first_row, colA_start, colA_end, colB_start, colB_end);

                            if (fabs(value) > zero_tol)
                            {
                                global_row = B->local_to_global[colB];
                                global_col = colA + A->first_col_diag;
                                tmp = {global_row, global_col, value};
                                send_buffer[i].push_back(tmp);
                            }
                        }
                    }
            
                    for (index_t colA = 0; colA < A->offd_num_cols; colA++)
                    {
                        colA_start = A_offd->indptr[colA];
                        colA_end = A_offd->indptr[colA+1];

                        if (colA_start < colA_end)
                        {
                            value = dot<index_t, index_t>(A_offd, B_offd, A->first_row, B->first_row, colA_start, colA_end, colB_start, colB_end);
                            if (fabs(value) > zero_tol)
                            {
                                global_row = B->local_to_global[colB];
                                global_col = A->local_to_global[colA];
                                tmp = {global_row, global_col, value};
                                send_buffer[i].push_back(tmp);
                            }
                        }
                    }
                }
            }
            // Send B-values to distant processor
            MPI_Isend(send_buffer[i].data(), send_buffer[i].size(),
                coo_type, send_proc, 1111, A->comm_mat, &send_requests[i]);
        }
        else
        {
            MPI_Isend(send_buffer[i].data(), 0, coo_type, send_proc, 1111, A->comm_mat, &send_requests[i]);
        }
    }
   
    // Multiply A_diag * B_diag
    seq_mm_T<index_t, index_t, index_t>(A_diag, B_diag, C, 
        A->first_row, B->first_row, A->first_col_diag);

    if (B->offd_num_cols)
    {
        seq_mm_T<index_t, index_t, std::vector<index_t>>(A_offd, B_diag, C, A->first_row, B->first_row, A->local_to_global);
    }

    // Receive messages and add to C
    count = 0;
    n_recv = 0;
    while (n_recv < num_recvs)
    {
        //Probe for messages, and recv any found
        MPI_Iprobe(MPI_ANY_SOURCE, 1111, A->comm_mat, &avail_flag, &recv_status);
        if (avail_flag)
        {
            // Get size of message in buffer
            MPI_Get_count(&recv_status, coo_type, &count);

            // Get process message comes from
            index_t proc = recv_status.MPI_SOURCE;

            // Receive message into buffer
            Element recv_buffer[count];
            MPI_Recv(&recv_buffer, count, coo_type, MPI_ANY_SOURCE, 1111, A->comm_mat, &recv_status);

            // Add values to C
            for (index_t i = 0; i < count; i++)
            {
                C->add_value(recv_buffer[i].row - C->first_row, recv_buffer[i].col, recv_buffer[i].value);
            }

            // Increment number of recvs
            n_recv++;
        }
    } 

    // Free custom COO datatype (finished communication)
    MPI_Type_free(&coo_type);

    // Convert matrices back to original format
    A_diag->convert(format_ad);
    if (A->offd_num_cols)
    {
        A_offd->convert(format_ao);
    }
    B_diag->convert(format_bd);
    if (B->offd_num_cols)
    {
        B_offd->convert(format_bo);
    }

    // Finalize parallel output matrix
    C->finalize(0);
    *_C = C;

    // Wait for sends to finish, then delete buffer
    if (num_sends)
    {
	    MPI_Waitall(send_procs.size(), send_requests, MPI_STATUS_IGNORE);
        delete[] send_buffer;
        delete[] send_requests; 
    }*/
}  
