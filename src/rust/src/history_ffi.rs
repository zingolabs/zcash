use std::{convert::TryFrom, slice};

use libc::{c_uchar, size_t};
use zcash_history::{Entry as MMREntry, Tree as MMRTree, Version, V1, V2};
use zcash_primitives::consensus::BranchId;

/// Switch the tree version on the epoch it is for.
fn dispatch<T>(cbranch: u32, v1: impl FnOnce() -> T, v2: impl FnOnce() -> T) -> T {
    match BranchId::try_from(cbranch).unwrap() {
        BranchId::Sprout
        | BranchId::Overwinter
        | BranchId::Sapling
        | BranchId::Heartwood
        | BranchId::Canopy => v1(),
        _ => v2(),
    }
}

fn construct_mmr_tree<V: Version>(
    // Consensus branch id
    cbranch: u32,
    // Length of tree in array representation
    t_len: u32,

    // Indices of provided tree nodes, length of p_len+e_len
    ni_ptr: *const u32,
    // Provided tree nodes data, length of p_len+e_len
    n_ptr: *const [c_uchar; zcash_history::MAX_ENTRY_SIZE],

    // Peaks count
    p_len: size_t,
    // Extra nodes loaded (for deletion) count
    e_len: size_t,
) -> Result<MMRTree<V>, &'static str> {
    let (indices, nodes) = unsafe {
        (
            slice::from_raw_parts(ni_ptr, p_len + e_len),
            slice::from_raw_parts(n_ptr, p_len + e_len),
        )
    };

    let mut peaks: Vec<_> = indices
        .iter()
        .zip(nodes.iter())
        .map(
            |(index, node)| match MMREntry::from_bytes(cbranch, &node[..]) {
                Ok(entry) => Ok((*index, entry)),
                Err(_) => Err("Invalid encoding"),
            },
        )
        .collect::<Result<_, _>>()?;
    let extra = peaks.split_off(p_len);

    Ok(MMRTree::new(t_len, peaks, extra))
}

#[no_mangle]
pub extern "system" fn librustzcash_mmr_append(
    // Consensus branch id
    cbranch: u32,
    // Length of tree in array representation
    t_len: u32,
    // Indices of provided tree nodes, length of p_len
    ni_ptr: *const u32,
    // Provided tree nodes data, length of p_len
    n_ptr: *const [c_uchar; zcash_history::MAX_ENTRY_SIZE],
    // Peaks count
    p_len: size_t,
    // New node pointer
    nn_ptr: *const [u8; zcash_history::MAX_NODE_DATA_SIZE],
    // Return of root commitment
    rt_ret: *mut [u8; 32],
    // Return buffer for appended leaves, should be pre-allocated of ceiling(log2(t_len)) length
    buf_ret: *mut [c_uchar; zcash_history::MAX_NODE_DATA_SIZE],
) -> u32 {
    dispatch(
        cbranch,
        || {
            librustzcash_mmr_append_inner::<V1>(
                cbranch, t_len, ni_ptr, n_ptr, p_len, nn_ptr, rt_ret, buf_ret,
            )
        },
        || {
            librustzcash_mmr_append_inner::<V2>(
                cbranch, t_len, ni_ptr, n_ptr, p_len, nn_ptr, rt_ret, buf_ret,
            )
        },
    )
}

fn librustzcash_mmr_append_inner<V: Version>(
    // Consensus branch id
    cbranch: u32,
    // Length of tree in array representation
    t_len: u32,
    // Indices of provided tree nodes, length of p_len
    ni_ptr: *const u32,
    // Provided tree nodes data, length of p_len
    n_ptr: *const [c_uchar; zcash_history::MAX_ENTRY_SIZE],
    // Peaks count
    p_len: size_t,
    // New node pointer
    nn_ptr: *const [u8; zcash_history::MAX_NODE_DATA_SIZE],
    // Return of root commitment
    rt_ret: *mut [u8; 32],
    // Return buffer for appended leaves, should be pre-allocated of ceiling(log2(t_len)) length
    buf_ret: *mut [c_uchar; zcash_history::MAX_NODE_DATA_SIZE],
) -> u32 {
    let new_node_bytes: &[u8; zcash_history::MAX_NODE_DATA_SIZE] = unsafe {
        match nn_ptr.as_ref() {
            Some(r) => r,
            None => {
                return 0;
            } // Null pointer passed, error
        }
    };

    let mut tree = match construct_mmr_tree::<V>(cbranch, t_len, ni_ptr, n_ptr, p_len, 0) {
        Ok(t) => t,
        _ => {
            return 0;
        } // error
    };

    let node = match V::from_bytes(cbranch, &new_node_bytes[..]) {
        Ok(node) => node,
        _ => {
            return 0;
        } // error
    };

    let appended = match tree.append_leaf(node) {
        Ok(appended) => appended,
        _ => {
            return 0;
        }
    };

    let return_count = appended.len();

    let root_node = tree
        .root_node()
        .expect("Just added, should resolve always; qed");
    unsafe {
        *rt_ret = V::hash(root_node.data());

        for (idx, next_buf) in slice::from_raw_parts_mut(buf_ret, return_count as usize)
            .iter_mut()
            .enumerate()
        {
            V::write(
                tree.resolve_link(appended[idx])
                    .expect("This was generated by the tree and thus resolvable; qed")
                    .data(),
                &mut &mut next_buf[..],
            )
            .expect("Write using cursor with enough buffer size cannot fail; qed");
        }
    }

    return_count as u32
}

#[no_mangle]
pub extern "system" fn librustzcash_mmr_delete(
    // Consensus branch id
    cbranch: u32,
    // Length of tree in array representation
    t_len: u32,
    // Indices of provided tree nodes, length of p_len+e_len
    ni_ptr: *const u32,
    // Provided tree nodes data, length of p_len+e_len
    n_ptr: *const [c_uchar; zcash_history::MAX_ENTRY_SIZE],
    // Peaks count
    p_len: size_t,
    // Extra nodes loaded (for deletion) count
    e_len: size_t,
    // Return of root commitment
    rt_ret: *mut [u8; 32],
) -> u32 {
    dispatch(
        cbranch,
        || librustzcash_mmr_delete_inner::<V1>(cbranch, t_len, ni_ptr, n_ptr, p_len, e_len, rt_ret),
        || librustzcash_mmr_delete_inner::<V2>(cbranch, t_len, ni_ptr, n_ptr, p_len, e_len, rt_ret),
    )
}

fn librustzcash_mmr_delete_inner<V: Version>(
    // Consensus branch id
    cbranch: u32,
    // Length of tree in array representation
    t_len: u32,
    // Indices of provided tree nodes, length of p_len+e_len
    ni_ptr: *const u32,
    // Provided tree nodes data, length of p_len+e_len
    n_ptr: *const [c_uchar; zcash_history::MAX_ENTRY_SIZE],
    // Peaks count
    p_len: size_t,
    // Extra nodes loaded (for deletion) count
    e_len: size_t,
    // Return of root commitment
    rt_ret: *mut [u8; 32],
) -> u32 {
    let mut tree = match construct_mmr_tree::<V>(cbranch, t_len, ni_ptr, n_ptr, p_len, e_len) {
        Ok(t) => t,
        _ => {
            return 0;
        } // error
    };

    let truncate_len = match tree.truncate_leaf() {
        Ok(v) => v,
        _ => {
            return 0;
        } // Error
    };

    unsafe {
        *rt_ret = V::hash(
            tree.root_node()
                .expect("Just generated without errors, root should be resolving")
                .data(),
        );
    }

    truncate_len
}

#[no_mangle]
pub extern "system" fn librustzcash_mmr_hash_node(
    cbranch: u32,
    n_ptr: *const [u8; zcash_history::MAX_NODE_DATA_SIZE],
    h_ret: *mut [u8; 32],
) -> u32 {
    dispatch(
        cbranch,
        || librustzcash_mmr_hash_node_inner::<V1>(cbranch, n_ptr, h_ret),
        || librustzcash_mmr_hash_node_inner::<V2>(cbranch, n_ptr, h_ret),
    )
}

fn librustzcash_mmr_hash_node_inner<V: Version>(
    cbranch: u32,
    n_ptr: *const [u8; zcash_history::MAX_NODE_DATA_SIZE],
    h_ret: *mut [u8; 32],
) -> u32 {
    let node_bytes: &[u8; zcash_history::MAX_NODE_DATA_SIZE] = unsafe {
        match n_ptr.as_ref() {
            Some(r) => r,
            None => return 1,
        }
    };

    let node = match V::from_bytes(cbranch, &node_bytes[..]) {
        Ok(n) => n,
        _ => return 1, // error
    };

    unsafe {
        *h_ret = V::hash(&node);
    }

    0
}
