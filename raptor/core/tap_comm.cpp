// Copyright (c) 2015, Raptor Developer Team, University of Illinois at Urbana-Champaign
// License: Simplified BSD, http://opensource.org/licenses/BSD-2-Clause
#include "comm_pkg.hpp"

using namespace raptor;

/**************************************************************
*****   Split Off Proc Cols
**************************************************************
***** Splits off_proc_column_map into on_node_column_map and 
***** off_node_column map.  Also maps each of these columns to 
***** their corresponding process, and maps each local index
***** in on_node and off_node to off_proc
*****
***** Parameters
***** -------------
***** off_proc_column_map : std::vector<int>&
*****    Vector holding rank's off_proc_columns
***** off_proc_col_to_proc : std::vector<int>&
*****    Vector mapping rank's off_proc_columns to distant procs
***** on_node_column_map : std::vector<int>&
*****    Will be returned holding on_node columns
***** on_node_col_to_proc : std::vector<int>&
*****    Will be returned holding procs corresponding to on_node cols
***** on_node_to_off_proc : std::vector<int>&
*****    Will be returned holding map from on_node to off_proc
***** off_node_column_map : std::vector<int>&
*****    Will be returned holding off_node columns
***** off_node_col_to_node : std::vector<int>&
*****    Will be returned holding procs corresponding to off_node cols
***** off_node_to_off_proc : std::vector<int>&
*****    Will be returned holding map from off_node to off_proc
**************************************************************/
void TAPComm::split_off_proc_cols(const std::vector<int>& off_proc_column_map,
        const std::vector<int>& off_proc_col_to_proc,
        std::vector<int>& on_node_column_map,
        std::vector<int>& on_node_col_to_proc,
        std::vector<int>& on_node_to_off_proc,
        std::vector<int>& off_node_column_map,
        std::vector<int>& off_node_col_to_node,
        std::vector<int>& off_node_to_off_proc)
{
    int rank, rank_node, num_procs;
    int proc;
    int node;
    int global_col;
    int off_proc_num_cols = off_proc_column_map.size();

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
    rank_node = get_node(rank);

    // Reserve size in vectors
    on_node_column_map.reserve(off_proc_num_cols);
    on_node_col_to_proc.reserve(off_proc_num_cols);
    off_node_column_map.reserve(off_proc_num_cols);
    off_node_col_to_node.reserve(off_proc_num_cols);
    
    for (int i = 0; i < off_proc_num_cols; i++)
    {
        proc = off_proc_col_to_proc[i];
        node = get_node(proc);
        global_col = off_proc_column_map[i];
        if (node == rank_node)
        {
            on_node_column_map.push_back(global_col);
            on_node_col_to_proc.push_back(get_local_proc(proc));
            on_node_to_off_proc.push_back(i);
        }
        else
        {
            off_node_column_map.push_back(global_col);
            off_node_col_to_node.push_back(node);
            off_node_to_off_proc.push_back(i);
        }
    }
}

/**************************************************************
*****   Gather off node nodes
**************************************************************
***** Gathers nodes with which any local processes communicates
*****
***** Parameters
***** -------------
***** off_node_col_to_node : std::vector<int>&
*****    Vector holding rank's off_node_columns
***** recv_nodes : std::vector<int>&
*****    Returned holding all nodes with which any local
*****    process communicates (union of off_node_col_to_node)
**************************************************************/
void TAPComm::gather_off_node_nodes(const std::vector<int>& off_node_col_to_node,
        std::vector<int>& recv_nodes, std::vector<int>& nodal_num_local)
{
    int int_size = sizeof(int);
    int n_procs;
    int N = num_nodes / int_size;
    if (num_nodes % int_size)
    {
        N++;
    }
    std::vector<int> tmp_recv_nodes(N, 0);
    std::vector<int> nodal_recv_nodes(N, 0);
    std::vector<int> node_sizes(num_nodes, 0);
    std::vector<int> nodal_off_node_sizes;
    for (std::vector<int>::const_iterator it = off_node_col_to_node.begin();
            it != off_node_col_to_node.end(); ++it)
    {
        int idx = *it / int_size;
        int pos = *it % int_size;
        tmp_recv_nodes[idx] |= 1 << pos;
        node_sizes[*it]++;
    }

    MPI_Allreduce(tmp_recv_nodes.data(), nodal_recv_nodes.data(), N, MPI_INT,
            MPI_BOR, local_comm);

    recv_nodes.resize(num_nodes);
    int ctr = 0;
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < int_size; j++)
        {
            if ((nodal_recv_nodes[i] >> j) & 1)
            {
                recv_nodes[ctr++] = i*int_size + j;
            }
        }
    }
    if (ctr)
    {
        recv_nodes.resize(ctr);
        nodal_num_local.resize(ctr, 1);

        // Collect the number of bytes sent to each node
        nodal_off_node_sizes.resize(ctr);
        for (int i = 0; i < ctr; i++)
        {
            int node = recv_nodes[i];
            nodal_off_node_sizes[i] = node_sizes[node];
        }
        MPI_Allreduce(MPI_IN_PLACE, nodal_off_node_sizes.data(), ctr, MPI_INT,
                MPI_SUM, local_comm);

        // Sort nodes, descending by msg size (find permutation)
        std::vector<int> p(ctr);
        std::iota(p.begin(), p.end(), 0);
        std::sort(p.begin(), p.end(), 
                [&](const int lhs, const int rhs)
                {
                    return nodal_off_node_sizes[lhs] > nodal_off_node_sizes[rhs];
                });

        // If not all processes are communicating and there are any
        // "large" (sent as eager or rendezvous), split these messages
        // across multiple local processes.
        // Split all rendezvous messages up.
        for (int i = 0; i < ctr; i++)
        {
            int idx = p[i];
            int size = nodal_off_node_sizes[idx];
            if (size > eager_cutoff)
            {
                n_procs = size / eager_cutoff;
                if (n_procs >= PPN)
                {
                    n_procs = ideal_n_comm;
                }
            }
            else if (size > short_cutoff && ctr < PPN)
            {
                n_procs = size / short_cutoff;
                if (n_procs >= PPN)
                {
                    n_procs = PPN;
                }
            }
            else
            {
                break;
            }
            nodal_num_local[idx] = n_procs;
        } 

        // Sort recv nodes by total num bytes recvd from node
        std::vector<bool> done(ctr);
        for (int i = 0; i < ctr; i++)
        {
            if (done[i]) continue;

            done[i] = true;
            int prev_j = i;
            int j = p[i];
            while (i != j)
            {
                std::swap(recv_nodes[prev_j], recv_nodes[j]);
                std::swap(nodal_num_local[prev_j], nodal_num_local[j]);
                done[j] = true;
                prev_j = j;
                j = p[j];
            }
        }
    }
    else
    {
        recv_nodes.clear();
    }
}   

/**************************************************************
*****   Find global comm procs
**************************************************************
***** Determine which processes with which rank will communicate
***** during inter-node communication
*****
***** Parameters
***** -------------
***** recv_nodes : std::vector<int>&
*****    All nodes with which any local process communicates 
***** send_procs : std::vector<int>&
*****    Returns with all off_node processes to which rank sends
***** recv_procs : std::vector<int>&
*****    Returns with all off_node process from which rank recvs
**************************************************************/
void TAPComm::find_global_comm_procs(const std::vector<int>& recv_nodes,
        std::vector<int>& nodal_num_local,
        std::vector<int>& send_procs, std::vector<int>& recv_procs)
{
    int rank;
    int local_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_rank(local_comm, &local_rank);

    int n_sends, n_recvs;
    int proc, node;
    int finished, msg_avail;
    int recvbuf;
    int n_recv_nodes;
    int n_send_procs;
    MPI_Status recv_status;
    MPI_Request barrier_request;

    std::vector<int> node_list(num_nodes, 0);
    std::vector<int> sendbuf;
    std::vector<int> send_sizes(PPN);
    std::vector<int> send_displs(PPN+1);
    std::vector<MPI_Request> requests;

    n_recv_nodes = recv_nodes.size();
    requests.resize(n_recv_nodes / PPN + 1, MPI_REQUEST_NULL);
    n_recvs = 0;

    int ctr = 0;
    for (int i = 0; i < n_recv_nodes; i++)
    {
        for (int j = 0; j < nodal_num_local[i]; j++)
        {
            if (ctr++ % PPN == local_rank)
            {
                node = recv_nodes[i];
                proc = get_global_proc(node, local_rank);
                MPI_Issend(&(recv_nodes[i]), 1, MPI_INT, proc, 9876, MPI_COMM_WORLD,
                        &(requests[n_recvs++]));
            }
        }
    }

    if (n_recvs)
    {
        MPI_Testall(n_recvs, requests.data(), &finished, MPI_STATUSES_IGNORE);
        while (!finished)
        {
            MPI_Iprobe(MPI_ANY_SOURCE, 9876, MPI_COMM_WORLD, &msg_avail, &recv_status);
            if (msg_avail)
            {
                proc = recv_status.MPI_SOURCE;
                MPI_Recv(&recvbuf, 1, MPI_INT, proc, 9876, MPI_COMM_WORLD, 
                        &recv_status);
                sendbuf.push_back(proc);
            }
            MPI_Testall(n_recvs, requests.data(), &finished, MPI_STATUSES_IGNORE);
        }    
    }
    MPI_Ibarrier(MPI_COMM_WORLD, &barrier_request);
    MPI_Test(&barrier_request, &finished, MPI_STATUS_IGNORE);
    while (!finished)
    {
        MPI_Iprobe(MPI_ANY_SOURCE, 9876, MPI_COMM_WORLD, &msg_avail, &recv_status);
        if (msg_avail)
        {
            proc = recv_status.MPI_SOURCE;
            MPI_Recv(&recvbuf, 1, MPI_INT, proc, 9876, MPI_COMM_WORLD, &recv_status);
            sendbuf.push_back(proc);
        }
        MPI_Test(&barrier_request, &finished, MPI_STATUS_IGNORE);
    }

    // Gather all procs that node must send to
    n_sends = sendbuf.size();
    MPI_Allgather(&n_sends, 1, MPI_INT, send_sizes.data(), 1, MPI_INT, local_comm);
    send_displs[0] = 0;
    for (int i = 0; i < PPN; i++)
    {
        send_displs[i+1] = send_displs[i] + send_sizes[i];
    } 
    send_procs.resize(send_displs[PPN]);
    MPI_Allgatherv(sendbuf.data(), n_sends, MPI_INT, send_procs.data(), 
            send_sizes.data(), send_displs.data(), MPI_INT, local_comm);

    n_send_procs = send_procs.size();

    // Distribute send_procs across local procs
    n_sends = 0;
//    for (int i = PPN - local_rank - 1; i < send_procs.size(); i += PPN)
    for (int i = local_rank; i < n_send_procs; i += PPN)
    {
        send_procs[n_sends++] = send_procs[i];
    }
    if (n_sends)
    {
        // Each process sends to all procs in "send_procs" 
        send_procs.resize(n_sends);
        requests.resize(n_sends);
        for (int i = 0; i < n_sends; i++)
        {
            proc = send_procs[i];
            MPI_Isend(&(send_procs[i]), 1, MPI_INT, proc, 6789, MPI_COMM_WORLD,
                    &(requests[i]));
        }
    }
    else
    {
        send_procs.clear();
    }

    // Recv processes from which rank must recv
    for (int i = 0; i < n_recvs; i++)
    {
        MPI_Probe(MPI_ANY_SOURCE, 6789, MPI_COMM_WORLD, &recv_status);
        proc = recv_status.MPI_SOURCE;
        MPI_Recv(&recvbuf, 1, MPI_INT, proc, 6789, MPI_COMM_WORLD, &recv_status);
        recv_procs.push_back(proc);
    }

    // Wait for sends to complete
    if (n_sends)
    {
        MPI_Waitall(n_sends, requests.data(), MPI_STATUSES_IGNORE);
    }
}

/**************************************************************
*****   Form local_R_par_comm
**************************************************************
***** Find which local processes recv needed vector values
***** from inter-node communication
*****
***** Parameters
***** -------------
***** off_node_column_map : std::vector<int>&
*****    Columns that correspond to values stored off_node 
***** off_node_col_to_node : std::vector<int>&
*****    Nodes corresponding to each value in off_node_column_map
***** recv_nodes : std::vector<int>&
*****    All nodes with which any local process communicates 
***** orig_nodes : std::vector<int>&
*****    Returns nodes on which local_R_par_comm->send_data->indices
*****    originate (needed in forming global communication)
**************************************************************/
void TAPComm::form_local_R_par_comm(const std::vector<int>& off_node_column_map,
        const std::vector<int>& off_node_col_to_node,
        const std::vector<int>& recv_nodes, std::vector<int>& nodal_num_local,
        std::vector<int>& orig_nodes)
{
    int local_rank;
    MPI_Comm_rank(local_comm, &local_rank);

    int off_node_num_cols = off_node_column_map.size();
    int num_recv_nodes = recv_nodes.size();
    int node, local_proc;
    int idx, proc;
    int start_ctr, ctr;
    int local_num_sends;
    int recv_start, recv_end;
    int recv_proc, recv_size;
    int count;
    MPI_Status recv_status;

    std::vector<int> node_to_idx(num_nodes);

    std::vector<int> node_to_proc(num_recv_nodes);
    std::vector<int> proc_idx(num_recv_nodes, 0);

    std::vector<int> local_recv_procs(PPN, 0);
    std::vector<int> local_recv_sizes(PPN, 0);
    std::vector<int> local_send_procs(PPN);
    std::vector<int> local_recv_displs(PPN+1, 0);
    std::vector<int> local_recv_indices(off_node_num_cols);
    std::vector<int> off_node_col_to_lcl_proc(off_node_num_cols);

    // Map nodes to procs
    ctr = 0;
    for (int i = 0; i < num_recv_nodes; i++)
    {
        node = recv_nodes[i];
        node_to_idx[node] = i;

        local_proc = ctr % PPN;
        node_to_proc[i] = local_proc;
        ctr += nodal_num_local[i];
    }

    // Find number of recvd indices per local proc
    for (int i = 0; i < off_node_num_cols; i++)
    {
        node = off_node_col_to_node[i];
        idx = node_to_idx[node];

        local_proc = (node_to_proc[idx] + proc_idx[idx]) % PPN;
        if (proc_idx[idx] + 1 < nodal_num_local[idx])
            proc_idx[idx]++;
        else
            proc_idx[idx] = 0;

        local_recv_sizes[local_proc]++;
        off_node_col_to_lcl_proc[i] = local_proc;
    }
    // Create displs based on local_recv_sizes
    local_recv_displs[0] = 0;
    for (int i = 0; i < PPN; i++)
    {
        local_recv_displs[i+1] = local_recv_displs[i] + local_recv_sizes[i];
        local_recv_sizes[i] = 0;
    }
    // Add columns to local_recv_indices in location according to
    // local_recv_displs
    for (int i = 0; i < off_node_num_cols; i++)
    {
        local_proc = off_node_col_to_lcl_proc[i];
        idx = local_recv_displs[local_proc] + local_recv_sizes[local_proc]++;
        local_recv_indices[idx] = i;
    }

    // Add recv_data for local_R_par_comm
    for (int i = 0; i < PPN; i++)
    {
        recv_start = local_recv_displs[i];
        recv_size = local_recv_displs[i+1] - recv_start;
        if (recv_size)
        {
            local_R_par_comm->recv_data->add_msg(i, recv_size, 
                    &(local_recv_indices[recv_start]));
            local_recv_procs[i] = 1;
        }
    }
    local_R_par_comm->recv_data->finalize();

    // On node communication-- scalable to do all reduce to find number of
    // local processes to send to :)
    MPI_Allreduce(local_recv_procs.data(), local_send_procs.data(), PPN, MPI_INT,
            MPI_SUM, local_comm);
    local_num_sends = local_send_procs[local_rank];

    // Send recv_indices to each recv_proc along with their origin 
    // node
    std::vector<int> send_buffer(2*local_R_par_comm->recv_data->size_msgs);
    ctr = 0;
    start_ctr = 0;
    for (int i = 0; i < local_R_par_comm->recv_data->num_msgs; i++)
    {
        recv_proc = local_R_par_comm->recv_data->procs[i];
        recv_start = local_R_par_comm->recv_data->indptr[i];
        recv_end = local_R_par_comm->recv_data->indptr[i+1];
        for (int j = recv_start; j < recv_end; j++)
        {
            idx = local_recv_indices[j];
            send_buffer[ctr++] = off_node_column_map[idx];
        }
        for (int j = recv_start; j < recv_end; j++)
        {
            idx = local_recv_indices[j];
            send_buffer[ctr++] = off_node_col_to_node[idx];
        }
        MPI_Isend(&(send_buffer[start_ctr]), 2*(recv_end - recv_start),
                MPI_INT, recv_proc, 6543, local_comm, 
                &(local_R_par_comm->recv_data->requests[i]));
        start_ctr = ctr;
    }

    // Recv messages from local processes and add to send_data
    ctr = 0;
    for (int i = 0; i < local_num_sends; i++)
    {
        MPI_Probe(MPI_ANY_SOURCE, 6543, local_comm, &recv_status);
        MPI_Get_count(&recv_status, MPI_INT, &count);
        proc = recv_status.MPI_SOURCE;
        int recvbuf[count];
        MPI_Recv(recvbuf, count, MPI_INT, proc, 6543, local_comm,
                &recv_status);
        local_R_par_comm->send_data->add_msg(proc, count / 2, recvbuf);
        start_ctr = count / 2;
        // Add orig nodes for each recvd col (need to know this for
        // global communication setup)
        for (int j = start_ctr; j < count; j++)
        {
            orig_nodes.push_back(recvbuf[j]);
        }
    }
    local_R_par_comm->send_data->finalize();

    // Wait for all sends to complete
    MPI_Waitall(local_R_par_comm->recv_data->num_msgs,
            local_R_par_comm->recv_data->requests,
            MPI_STATUS_IGNORE);
}

/**************************************************************
*****   Find global comm procs
**************************************************************
***** Determine which processes with which rank will communicate
***** during inter-node communication
*****
***** Parameters
***** -------------
***** recv_nodes : std::vector<int>&
*****    All nodes with which any local process communicates 
***** send_procs : std::vector<int>&
*****    Returns with all off_node processes to which rank sends
***** recv_procs : std::vector<int>&
*****    Returns wiht all off_node process from which rank recvs
**************************************************************/
void TAPComm::form_global_par_comm(const std::vector<int>& send_procs, 
        const std::vector<int>& recv_procs, const std::vector<int>& orig_nodes)
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int proc, node;
    int node_idx, idx;
    int start, end, size;
    int ctr;
    int count;
    int n_send_procs = send_procs.size();
    int n_recv_procs = recv_procs.size();
    MPI_Status recv_status;

    // Send origin nodes of each column recvd in local_R_par_comm
    std::vector<int> node_sizes(num_nodes, 0);
    std::vector<int> node_recv_idx(num_nodes);
    std::vector<int> node_recv_sizes(recv_procs.size(), 0);
    std::vector<int> node_recv_displs(recv_procs.size()+1, 0);
    std::vector<int> node_recv_indices(local_R_par_comm->send_data->size_msgs);

    // Find how many values are send to local processes from each node
    // THIS CONTAINS DUPLICATES
    for (int i = 0; i < local_R_par_comm->send_data->size_msgs; i++)
    {
        node = orig_nodes[i];
        node_sizes[node]++;
    }
    // Create TEMPORARY displs from recv node sizes
    // NEED TO REMOVE DUPLICATES
    node_recv_displs[0] = 0;
    for (int i = 0; i < n_recv_procs; i++)
    {
        proc = recv_procs[i];
        node = get_node(proc);
        node_recv_idx[node] = i;
        node_recv_displs[i+1] = node_recv_displs[i] + node_sizes[node];
    }

    // Sort global indices by node from which they are recvd
    for (int i = 0; i < local_R_par_comm->send_data->size_msgs; i++)
    {
        node = orig_nodes[i];
        node_idx = node_recv_idx[node];
        idx = node_recv_displs[node_idx] + node_recv_sizes[node_idx]++;
        node_recv_indices[idx] = local_R_par_comm->send_data->indices[i];
    }

    for (int i = 0; i < n_recv_procs; i++)
    {
        proc = recv_procs[i];
        start = node_recv_displs[i];
        end = node_recv_displs[i+1];
        size = end - start;
        if (size)
        {
            std::sort(node_recv_indices.begin() + start, node_recv_indices.begin() + end);
            int recv_buffer[size];
            ctr = 1;
            recv_buffer[0] = node_recv_indices[start];
            for (int j = start+1; j < end; j++)
            {
                if (node_recv_indices[j] != node_recv_indices[j-1])
                {
                    recv_buffer[ctr++] = node_recv_indices[j];
                }
            }
            global_par_comm->recv_data->add_msg(proc, ctr, recv_buffer);
        }
    }
    global_par_comm->recv_data->finalize();

    // Send recv indices to each recv proc
    for (int i = 0; i < global_par_comm->recv_data->num_msgs; i++)
    {
        proc = global_par_comm->recv_data->procs[i];
        start = global_par_comm->recv_data->indptr[i];
        end = global_par_comm->recv_data->indptr[i+1];
        MPI_Isend(&(global_par_comm->recv_data->indices[start]),
                end - start, MPI_INT, proc, 5432, MPI_COMM_WORLD,
                &(global_par_comm->recv_data->requests[i]));
    }

    for (int i = 0; i < n_send_procs; i++)
    {
        proc = send_procs[i];
        MPI_Probe(proc, 5432, MPI_COMM_WORLD, &recv_status);
        MPI_Get_count(&recv_status, MPI_INT, &count);
        int recvbuf[count];
        MPI_Recv(recvbuf, count, MPI_INT, proc, 5432, MPI_COMM_WORLD, &recv_status);
        global_par_comm->send_data->add_msg(proc, count, recvbuf);
    }
    global_par_comm->send_data->finalize();

    MPI_Waitall(global_par_comm->recv_data->num_msgs,
            global_par_comm->recv_data->requests,
            MPI_STATUS_IGNORE);

}

/**************************************************************
*****   Form local_S_par_comm
**************************************************************
***** Find which local processes the values originating on rank
***** must be sent to, and which processes store values rank must
***** send as inter-node communication.
*****
***** Parameters
***** -------------
***** first_local_col : int
*****    First column local to rank 
**************************************************************/
void TAPComm::form_local_S_par_comm(const int first_local_col)
{
    int rank;
    int local_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_rank(local_comm, &local_rank);

    // Find local_col_starts for all procs local to node, and sort
    int start, end;
    int proc;
    int global_col;
    int ctr, idx;
    int size;

    std::vector<int> local_col_starts(PPN);
    std::vector<int> local_procs(PPN);
    std::vector<int> orig_procs;
    std::vector<int> proc_indices;
    std::vector<int> proc_sizes(PPN, 0);
    std::vector<int> recv_procs(PPN, 0);
    std::vector<int> proc_displs(PPN+1);

    if (global_par_comm->send_data->num_msgs)
    {
        orig_procs.resize(global_par_comm->send_data->size_msgs);
        proc_indices.resize(global_par_comm->send_data->size_msgs);
    }

    // Find first local col on each local proc 
    // (for determining where values originate)
    // Already in ascending order
    MPI_Allgather(&first_local_col, 1, MPI_INT, local_col_starts.data(), 1, MPI_INT,
           local_comm);

    // Find all column indices originating on local procs
    for (int i = 0; i < global_par_comm->send_data->num_msgs; i++)
    {
        start = global_par_comm->send_data->indptr[i];
        end = global_par_comm->send_data->indptr[i+1];
        proc = 0;
        for (int j = start; j < end; j++)
        {
            global_col = global_par_comm->send_data->indices[j];
            while (proc + 1 < PPN &&
                    global_col >= local_col_starts[proc + 1])
            {
                proc++;
            }

            orig_procs[j] = proc;
            proc_sizes[proc]++;
            recv_procs[proc] = 1;
        }
    }

    // Reduce recv_procs to how many msgs rank will recv
    MPI_Allreduce(MPI_IN_PLACE, recv_procs.data(), PPN, MPI_INT, MPI_SUM, local_comm);
    int n_recvs = recv_procs[local_rank];

    proc_displs[0] = 0;
    for (int i = 0; i < PPN; i++)
    {
        proc_displs[i+1] = proc_displs[i] + proc_sizes[i];
        proc_sizes[i] = 0;
    }

    for (int i = 0; i < global_par_comm->send_data->size_msgs; i++)
    {
        proc = orig_procs[i];
        idx = proc_displs[proc] + proc_sizes[proc]++;
        proc_indices[idx] = global_par_comm->send_data->indices[i];
    }

    for (int i = 0; i < PPN; i++)
    {
        start = proc_displs[i];
        end = proc_displs[i+1];
        size = end - start;
        if (size)
        {
            std::sort(proc_indices.begin() + start, proc_indices.begin() + end);
            int recv_buffer[size];
            ctr = 1;
            recv_buffer[0] = proc_indices[start];
            for (int j = start+1; j < end; j++)
            {
                if (proc_indices[j] != proc_indices[j-1])
                {
                    recv_buffer[ctr++] = proc_indices[j];
                }
            }
            local_S_par_comm->recv_data->add_msg(i, ctr, recv_buffer);
        }
    }
    local_S_par_comm->recv_data->finalize();

    for (int i = 0; i < local_S_par_comm->recv_data->num_msgs; i++)
    {
        proc = local_S_par_comm->recv_data->procs[i];
        start = local_S_par_comm->recv_data->indptr[i];
        end = local_S_par_comm->recv_data->indptr[i+1];
        MPI_Isend(&(local_S_par_comm->recv_data->indices[start]), 
                end - start, MPI_INT, proc, 4321, local_comm,
                &(local_S_par_comm->recv_data->requests[i]));
    }

    int count;
    MPI_Status recv_status;
    for (int i = 0; i < n_recvs; i++)
    {
        MPI_Probe(MPI_ANY_SOURCE, 4321, local_comm, &recv_status);
        MPI_Get_count(&recv_status, MPI_INT, &count);
        proc = recv_status.MPI_SOURCE;        
        int recvbuf[count];
        MPI_Recv(recvbuf, count, MPI_INT, proc, 4321, local_comm, &recv_status);
        local_S_par_comm->send_data->add_msg(proc, count, recvbuf);
    }
    local_S_par_comm->send_data->finalize();

    MPI_Waitall(local_S_par_comm->recv_data->num_msgs,
            local_S_par_comm->recv_data->requests,
            MPI_STATUS_IGNORE);
}

/**************************************************************
*****   Adjust Send Indices
**************************************************************
***** Adjust send indices from global row index to index of 
***** global column in previous recv buffer.  
*****
***** Parameters
***** -------------
***** first_local_row : int
*****    First row local to rank 
**************************************************************/
void TAPComm::adjust_send_indices(const int first_local_row)
{
    int idx;

    // Update global row index with local row to send 
    for (int i = 0; i < local_S_par_comm->send_data->size_msgs; i++)
    {
        local_S_par_comm->send_data->indices[i] -= first_local_row;
    }

    // Update global_par_comm->send_data->indices (global rows) to 
    // index of global row in local_S_par_comm->recv_data->indices
    std::map<int, int> S_global_to_local;
    for (int i = 0; i < local_S_par_comm->recv_data->size_msgs; i++)
    {
        S_global_to_local[local_S_par_comm->recv_data->indices[i]] = i;
    }
    for (int i = 0; i < global_par_comm->send_data->size_msgs; i++)
    {
        idx = global_par_comm->send_data->indices[i];
        global_par_comm->send_data->indices[i] = S_global_to_local[idx];
    }

    // Update local_R_par_comm->send_data->indices (global_rows)
    // to index of global row in global_par_comm->recv_data
    std::map<int, int> global_to_local;
    for (int i = 0; i < global_par_comm->recv_data->size_msgs; i++)
    {
        global_to_local[global_par_comm->recv_data->indices[i]] = i;
    }
    for (int i = 0; i < local_R_par_comm->send_data->size_msgs; i++)
    {
        idx = local_R_par_comm->send_data->indices[i];
        local_R_par_comm->send_data->indices[i] = global_to_local[idx];
    }
}

/**************************************************************
*****  Form local_L_par_comm 
**************************************************************
***** Adjust send indices from global row index to index of 
***** global column in previous recv buffer.  
*****
***** Parameters
***** -------------
***** on_node_column_map : std::vector<int>&
*****    Columns corresponding to on_node processes
***** on_node_col_to_proc : std::vector<int>&
*****    On node process corresponding to each column
*****    in on_node_column_map
***** first_local_row : int
*****    First row local to rank 
**************************************************************/
void TAPComm::form_local_L_par_comm(const std::vector<int>& on_node_column_map,
        const std::vector<int>& on_node_col_to_proc, const int first_local_row)
{
    int local_rank;
    MPI_Comm_rank(local_comm, &local_rank);

    int on_node_num_cols = on_node_column_map.size();
    int prev_proc, prev_idx;
    int num_sends;
    int proc, start, end;
    int count;
    MPI_Status recv_status;
    std::vector<int> recv_procs(PPN, 0);

    if (on_node_num_cols)
    {
        prev_proc = on_node_col_to_proc[0];
        recv_procs[prev_proc] = 1;
        prev_idx = 0;
        for (int i = 1; i < on_node_num_cols; i++)
        {
            proc = on_node_col_to_proc[i];
            if (proc != prev_proc)
            {
                local_L_par_comm->recv_data->add_msg(prev_proc, i - prev_idx);
                prev_proc = proc;
                prev_idx = i;
                recv_procs[proc] = 1;
            }
        }
        local_L_par_comm->recv_data->add_msg(prev_proc, on_node_num_cols - prev_idx);
        local_L_par_comm->recv_data->finalize();
    }

    MPI_Allreduce(MPI_IN_PLACE, recv_procs.data(), PPN, MPI_INT, MPI_SUM, 
            local_comm);
    num_sends = recv_procs[local_rank];

    for (int i = 0; i < local_L_par_comm->recv_data->num_msgs; i++)
    {
        proc = local_L_par_comm->recv_data->procs[i];
        start = local_L_par_comm->recv_data->indptr[i];
        end = local_L_par_comm->recv_data->indptr[i+1];
        MPI_Isend(&(on_node_column_map[start]), end - start, MPI_INT, proc,
                7890, local_comm, &(local_L_par_comm->recv_data->requests[i]));
    }

    for (int i = 0; i < num_sends; i++)
    {
        MPI_Probe(MPI_ANY_SOURCE, 7890, local_comm, &recv_status);
        MPI_Get_count(&recv_status, MPI_INT, &count);
        proc = recv_status.MPI_SOURCE;
        int recvbuf[count];
        MPI_Recv(recvbuf, count, MPI_INT, proc, 7890, local_comm, &recv_status);
        for (int i = 0; i < count; i++)
        {
            recvbuf[i] -= first_local_row;
        }
        local_L_par_comm->send_data->add_msg(proc, count, recvbuf);
    }
    local_L_par_comm->send_data->finalize();
    
    MPI_Waitall(local_L_par_comm->recv_data->num_msgs,
            local_L_par_comm->recv_data->requests, MPI_STATUSES_IGNORE);
}

/**************************************************************
*****  Get Node 
**************************************************************
***** Find node on which global rank lies
*****
***** Returns
***** -------------
***** int : node on which proc lies
*****
***** Parameters
***** -------------
***** proc : int
*****    Global rank of process 
**************************************************************/
int TAPComm::get_node(int proc)
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank_ordering == 0)
    {
        return proc % num_nodes;
    }
    else if (rank_ordering == 1)
    {
        return proc / PPN;
    }
    else if (rank_ordering == 2)
    {
        if ((proc / num_nodes) % 2 == 0)
        {
            return proc % num_nodes;
        }
        else
        {
            return num_nodes - (proc % num_nodes) - 1;
        }
    }
    else
    { 
        if (rank == 0)
        {
            printf("This MPI rank ordering is not supported!\n");
        }
        return -1;
    }
}

/**************************************************************
*****  Get Local Proc 
**************************************************************
***** Find rank local to node from global rank
*****
***** Returns
***** -------------
***** int : rank local to processes on node
*****
***** Parameters
***** -------------
***** proc : int
*****    Global rank of process 
**************************************************************/
int TAPComm::get_local_proc(int proc)
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank_ordering == 0 || rank_ordering == 2)
    {
        return proc / num_nodes;
    }
    else if (rank_ordering == 1)
    {
        return proc % PPN;
    }
    else
    { 
        if (rank == 0)
        {
            printf("This MPI rank ordering is not supported!\n");
        }
        return -1;
    }
}

/**************************************************************
*****  Get Global Proc 
**************************************************************
***** Find global rank from node and local rank
*****
***** Returns
***** -------------
***** int : Global rank of process
*****
***** Parameters
***** -------------
***** node : int
*****    Node on which process lies 
***** local_proc : int
*****    Rank of process local to node
**************************************************************/
int TAPComm::get_global_proc(int node, int local_proc)
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank_ordering == 0)
    {
        return local_proc * num_nodes + node;
    }
    else if (rank_ordering == 1)
    {
        return local_proc + (node * PPN);
    }
    else if (rank_ordering == 2)
    {
        if ((rank / num_nodes) % 2 == 0)
        {
            return local_proc * num_nodes + node;
        }
        else
        {
            return local_proc * num_nodes + num_nodes - node - 1;                
        }
    }
    else
    { 
        if (rank == 0)
        {
            printf("This MPI rank ordering is not supported!\n");
        }
        return -1;
    }
}
