BEGIN;

DROP TABLE IF EXISTS ufs CASCADE;
CREATE TABLE ufs (
    pk       bigint        GENERATED ALWAYS AS IDENTITY,
    uf_date  date          NOT NULL,
    uf_value numeric(17,2) NOT NULL,
    created  timestamptz   NOT NULL DEFAULT NOW(),
    updated  timestamptz   NOT NULL DEFAULT NOW(),
    PRIMARY KEY (pk)
);
CREATE UNIQUE INDEX ON ufs(uf_date);


CREATE OR REPLACE FUNCTION actualizar_timestamp_modificacion()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


CREATE TRIGGER trigger_ufs_actualizado
BEFORE INSERT OR UPDATE ON ufs
FOR EACH ROW
EXECUTE FUNCTION actualizar_timestamp_modificacion();

COMMIT;
